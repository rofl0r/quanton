#define _POSIX_C_SOURCE 200809L

#include "quanton.h"

#include "lexbor/dom/interface.h"
#include "lexbor/dom/interfaces/element.h"
#include "lexbor/dom/interfaces/node.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#define VP_WIDTH      1000
#define VP_HEIGHT     680
#define MAX_ENTRIES   2048
#define ROW_HTML_MAX  512
#define TBODY_BUF_MAX (MAX_ENTRIES * ROW_HTML_MAX + 64)
#define FULL_HTML_MAX (TBODY_BUF_MAX + 2048)

typedef enum {
    FB_SORT_NAME,
    FB_SORT_SIZE,
    FB_SORT_MODIFIED,
    FB_SORT_TYPE
} fb_sort_t;

typedef struct {
    char      name[256];
    long long size;
    time_t    mtime;
    int       is_dir;
} fb_entry_t;

typedef struct {
    quanton_view_t  *view;
    lxb_dom_element_t *tbody;
    fb_sort_t        current_sort;
} fb_app_t;

static fb_entry_t g_entries[MAX_ENTRIES];
static int        g_nentries = 0;

static void load_cwd_entries(void)
{
    DIR           *dir;
    struct dirent *de;
    struct stat    st;

    dir = opendir(".");
    if (dir == NULL) {
        return;
    }

    while ((de = readdir(dir)) != NULL && g_nentries < MAX_ENTRIES) {
        if (de->d_name[0] == '.') {
            continue;
        }
        if (stat(de->d_name, &st) != 0) {
            continue;
        }
        strncpy(g_entries[g_nentries].name, de->d_name,
                sizeof(g_entries[0].name) - 1);
        g_entries[g_nentries].name[sizeof(g_entries[0].name) - 1] = '\0';
        g_entries[g_nentries].size  = (long long) st.st_size;
        g_entries[g_nentries].mtime = st.st_mtime;
        g_entries[g_nentries].is_dir = S_ISDIR(st.st_mode) ? 1 : 0;
        g_nentries++;
    }
    closedir(dir);
}

static int cmp_name(const void *a, const void *b)
{
    return strcmp(((const fb_entry_t *) a)->name,
                  ((const fb_entry_t *) b)->name);
}

static int cmp_size(const void *a, const void *b)
{
    long long da = ((const fb_entry_t *) a)->size;
    long long db = ((const fb_entry_t *) b)->size;
    return (da > db) - (da < db);
}

static int cmp_mtime(const void *a, const void *b)
{
    time_t ta = ((const fb_entry_t *) a)->mtime;
    time_t tb = ((const fb_entry_t *) b)->mtime;
    return (ta > tb) - (ta < tb);
}

static int cmp_type(const void *a, const void *b)
{
    const fb_entry_t *ea = (const fb_entry_t *) a;
    const fb_entry_t *eb = (const fb_entry_t *) b;

    if (ea->is_dir != eb->is_dir) {
        return eb->is_dir - ea->is_dir;
    }
    return strcmp(ea->name, eb->name);
}

static char *build_tbody_html(const fb_entry_t *ents, int n)
{
    char   *buf;
    size_t  pos = 0;
    int     i;
    char    date_buf[32];
    struct tm *tm_info;

    buf = malloc(TBODY_BUF_MAX);
    if (buf == NULL) {
        return NULL;
    }

    for (i = 0; i < n && pos + ROW_HTML_MAX < TBODY_BUF_MAX; i++) {
        tm_info = localtime(&ents[i].mtime);
        if (tm_info != NULL) {
            strftime(date_buf, sizeof(date_buf), "%Y-%m-%d %H:%M", tm_info);
        } else {
            date_buf[0] = '-';
            date_buf[1] = '\0';
        }
        pos += (size_t) snprintf(buf + pos, TBODY_BUF_MAX - pos,
                                 "<tr>"
                                 "<td>%s</td>"
                                 "<td>%s</td>"
                                 "<td>%lld</td>"
                                 "<td>%s</td>"
                                 "</tr>",
                                 ents[i].name,
                                 ents[i].is_dir ? "Directory" : "File",
                                 ents[i].size,
                                 date_buf);
    }
    buf[pos] = '\0';
    return buf;
}

static char *build_full_html(const fb_entry_t *ents, int n)
{
    char *tbody;
    char *buf;

    tbody = build_tbody_html(ents, n);
    if (tbody == NULL) {
        return NULL;
    }

    buf = malloc(FULL_HTML_MAX);
    if (buf == NULL) {
        free(tbody);
        return NULL;
    }

    snprintf(buf, FULL_HTML_MAX,
             "<html><body style=\"margin:12px;\">"
             "<table id=\"ft\" style=\"width:%dpx;border-collapse:collapse;\">"
             "<thead><tr>"
             "<th data-sort=\"name\" style=\"background-color:#d8e2f2;\">Name</th>"
             "<th data-sort=\"type\" style=\"background-color:#d8e2f2;\">Type</th>"
             "<th data-sort=\"size\" style=\"background-color:#d8e2f2;\">Size</th>"
             "<th data-sort=\"modified\" style=\"background-color:#d8e2f2;\">Modified</th>"
             "</tr></thead>"
             "<tbody id=\"ftb\">%s</tbody>"
             "</table>"
             "</body></html>",
             VP_WIDTH - 24,
             tbody);

    free(tbody);
    return buf;
}

static int render_sorted(quanton_view_t *view,
                         lxb_dom_element_t *tbody,
                         int (*cmpfn)(const void *, const void *))
{
    char      *tbody_html;
    int        rc;
    fb_entry_t sorted[MAX_ENTRIES];

    memcpy(sorted, g_entries, (size_t) g_nentries * sizeof(fb_entry_t));
    qsort(sorted, (size_t) g_nentries, sizeof(fb_entry_t), cmpfn);

    tbody_html = build_tbody_html(sorted, g_nentries);
    if (tbody_html == NULL) {
        fprintf(stderr, "OOM building tbody HTML\n");
        return -1;
    }

    rc = q_dom_set_inner_html(view, tbody, tbody_html, strlen(tbody_html));
    free(tbody_html);
    if (rc != 0) {
        fprintf(stderr, "q_dom_set_inner_html failed\n");
        return -1;
    }

    q_view_update(view);
    return 0;
}

static int render_by_sort(quanton_view_t *view, lxb_dom_element_t *tbody, fb_sort_t sort_key)
{
    int (*cmpfn)(const void *, const void *) = cmp_name;

    switch (sort_key) {
    case FB_SORT_NAME:
        cmpfn = cmp_name;
        break;
    case FB_SORT_SIZE:
        cmpfn = cmp_size;
        break;
    case FB_SORT_MODIFIED:
        cmpfn = cmp_mtime;
        break;
    case FB_SORT_TYPE:
        cmpfn = cmp_type;
        break;
    }

    return render_sorted(view, tbody, cmpfn);
}

static fb_sort_t sort_from_attr(const lxb_char_t *val, size_t len)
{
    if (len == 4 && memcmp(val, "name", 4) == 0) {
        return FB_SORT_NAME;
    }
    if (len == 4 && memcmp(val, "size", 4) == 0) {
        return FB_SORT_SIZE;
    }
    if (len == 8 && memcmp(val, "modified", 8) == 0) {
        return FB_SORT_MODIFIED;
    }
    if (len == 4 && memcmp(val, "type", 4) == 0) {
        return FB_SORT_TYPE;
    }
    return FB_SORT_NAME;
}

static void filebrowser_on_event(quanton_view_t *view,
                                 const q_event_t *event,
                                 void *userdata)
{
    fb_app_t *app = (fb_app_t *) userdata;
    lxb_dom_node_t *delegate;
    lxb_dom_element_t *el;
    const lxb_char_t *sort_val;
    size_t sort_len = 0;
    fb_sort_t sort_key;

    if (view == NULL || event == NULL || app == NULL || app->tbody == NULL) {
        return;
    }

    if (event->type != Q_EVENT_MOUSE_UP || event->mouse_button != 0) {
        return;
    }

    delegate = q_event_find_delegate(event->target, "data-sort");
    if (delegate == NULL || lxb_dom_node_type(delegate) != LXB_DOM_NODE_TYPE_ELEMENT) {
        return;
    }

    el = lxb_dom_interface_element(delegate);
    sort_val = lxb_dom_element_get_attribute(
        el, (const lxb_char_t *) "data-sort", 9, &sort_len);
    if (sort_val == NULL || sort_len == 0) {
        return;
    }

    sort_key = sort_from_attr(sort_val, sort_len);
    if (sort_key == app->current_sort) {
        return;
    }

    if (render_by_sort(view, app->tbody, sort_key) == 0) {
        app->current_sort = sort_key;
        printf("Sorted by %.*s\n", (int) sort_len, sort_val);
    }
}

int main(void)
{
    q_document_t    *doc;
    quanton_ctx_t    ctx;
    quanton_view_t   view;
    char            *html;
    lxb_dom_element_t *tbody;
    fb_entry_t       sorted[MAX_ENTRIES];
    fb_app_t         app;
    struct timespec  ts;

    load_cwd_entries();
    memcpy(sorted, g_entries, (size_t) g_nentries * sizeof(fb_entry_t));
    qsort(sorted, (size_t) g_nentries, sizeof(fb_entry_t), cmp_name);

    html = build_full_html(sorted, g_nentries);
    if (html == NULL) {
        fprintf(stderr, "OOM building full HTML\n");
        return 1;
    }

    doc = q_document_create();
    if (doc == NULL) {
        free(html);
        return 1;
    }
    if (q_document_load_html(doc, html, strlen(html), NULL) != 0) {
        free(html);
        q_document_destroy(doc);
        return 1;
    }
    free(html);

    memset(&ctx,  0, sizeof(ctx));
    memset(&view, 0, sizeof(view));
    memset(&app,  0, sizeof(app));

#if defined(QUANTON_BACKEND_X11)
    ctx.backend = &q_backend_x11;
#elif defined(QUANTON_BACKEND_SDL2)
    ctx.backend = &q_backend_sdl2;
#else
#error "Define QUANTON_BACKEND_X11 or QUANTON_BACKEND_SDL2"
#endif

    view.ctx        = &ctx;
    view.document   = doc;
    view.vp_width   = VP_WIDTH;
    view.vp_height  = VP_HEIGHT;

    app.view = &view;
    app.current_sort = FB_SORT_NAME;
    view.on_event = filebrowser_on_event;
    view.on_event_userdata = &app;

    if (ctx.backend->create_window(&view, VP_WIDTH, VP_HEIGHT,
                                   "quanton filebrowser") != 0) {
        q_document_destroy(doc);
        return 1;
    }

    q_dom_mark_dirty(&view, NULL,
                     (q_dirty_flags_t)(Q_DIRTY_LAYOUT | Q_DIRTY_PAINT));
    q_view_update(&view);

    tbody = q_dom_get_element_by_id(&view, "ftb");
    if (tbody == NULL) {
        ctx.backend->destroy_window(&view);
        q_document_destroy(doc);
        return 1;
    }
    app.tbody = tbody;

    ts.tv_sec = 0;
    ts.tv_nsec = 16L * 1000L * 1000L;
    while (!view.should_close) {
        ctx.backend->poll_events(&view);
        nanosleep(&ts, NULL);
    }

    ctx.backend->destroy_window(&view);
    q_layout_free_tree(view.layout_root);
    q_document_destroy(doc);
    return 0;
}
