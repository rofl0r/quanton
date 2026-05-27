/*
 * filebrowser_png.c — headless PNG file-browser demo
 *
 * Reads the current working directory, renders a sortable HTML table with
 * columns "Name", "Size", and "Modified" using the quanton PNG backend.
 *
 * Three output PNGs are produced:
 *   filebrowser_name.png  — sorted by file name
 *   filebrowser_size.png  — sorted by file size
 *   filebrowser_date.png  — sorted by modification time
 *
 * The re-sort is done via q_dom_set_inner_html() which replaces the <tbody>
 * content and marks the view dirty.  q_view_update() then rebuilds the layout,
 * repaints, and blits the result to disk — all in one call.
 *
 * Build:
 *   make filebrowser_png
 */

#define _POSIX_C_SOURCE 200809L

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "quanton.h"

/* ── constants ─────────────────────────────────────────────────────────── */

#define VP_WIDTH      900
#define VP_HEIGHT     600
#define MAX_ENTRIES   2048
/* per-row HTML: <tr><td>name</td><td>size</td><td>date</td></tr> ≤ ~400 B */
#define ROW_HTML_MAX  400
#define TBODY_BUF_MAX (MAX_ENTRIES * ROW_HTML_MAX + 64)
#define FULL_HTML_MAX (TBODY_BUF_MAX + 512)

/* ── file-entry type ────────────────────────────────────────────────────── */

typedef struct {
    char      name[256];
    long long size;
    time_t    mtime;
} fb_entry_t;

static fb_entry_t g_entries[MAX_ENTRIES];
static int        g_nentries = 0;

/* ── directory scan ─────────────────────────────────────────────────────── */

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
            continue; /* skip hidden files and . / .. */
        }
        if (stat(de->d_name, &st) != 0) {
            continue;
        }
        strncpy(g_entries[g_nentries].name, de->d_name,
                sizeof(g_entries[0].name) - 1);
        g_entries[g_nentries].name[sizeof(g_entries[0].name) - 1] = '\0';
        g_entries[g_nentries].size  = (long long) st.st_size;
        g_entries[g_nentries].mtime = st.st_mtime;
        g_nentries++;
    }
    closedir(dir);
}

/* ── comparators ─────────────────────────────────────────────────────────── */

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

/* ── HTML builders ────────────────────────────────────────────────────────── */

/*
 * Build the rows that belong inside <tbody>.
 * Returns a heap-allocated NUL-terminated string; caller must free().
 * Returns NULL on allocation failure.
 */
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
                                 "<td>%lld</td>"
                                 "<td>%s</td>"
                                 "</tr>",
                                 ents[i].name,
                                 ents[i].size,
                                 date_buf);
    }
    buf[pos] = '\0';
    return buf;
}

/*
 * Build the complete HTML document for the initial render.
 * Returns a heap-allocated NUL-terminated string; caller must free().
 */
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
             "<html><body>"
             "<table id=\"ft\" style=\"width:%dpx\">"
             "<thead><tr>"
             "<th>Name</th><th>Size</th><th>Modified</th>"
             "</tr></thead>"
             "<tbody id=\"ftb\">%s</tbody>"
             "</table>"
             "</body></html>",
             VP_WIDTH,
             tbody);

    free(tbody);
    return buf;
}

/* ── render helpers ───────────────────────────────────────────────────────── */

/*
 * Re-sort the entries and replace the tbody innerHTML, then trigger a full
 * relayout + repaint + PNG blit.
 *
 * The explicit q_view_update() call is intentional: the application controls
 * when the repaint happens, which makes it possible to batch multiple DOM
 * mutations before incurring the (expensive) relayout cost.
 */
static int render_sorted(quanton_view_t *view,
                         lxb_dom_element_t *tbody,
                         fb_entry_t *sorted, int n,
                         int (*cmpfn)(const void *, const void *),
                         const char *out_png)
{
    char *tbody_html;
    int   rc;

    memcpy(sorted, g_entries, (size_t) n * sizeof(fb_entry_t));
    qsort(sorted, (size_t) n, sizeof(fb_entry_t), cmpfn);

    tbody_html = build_tbody_html(sorted, n);
    if (tbody_html == NULL) {
        fprintf(stderr, "OOM building tbody HTML\n");
        return -1;
    }

    /* Replace tbody children; marks view Q_DIRTY_LAYOUT automatically */
    rc = q_dom_set_inner_html(view, tbody, tbody_html, strlen(tbody_html));
    free(tbody_html);
    if (rc != 0) {
        fprintf(stderr, "q_dom_set_inner_html failed\n");
        return -1;
    }

    /* Point the PNG backend at the new output file */
    free(view->window_handle);
    view->window_handle = strdup(out_png);
    if (view->window_handle == NULL) {
        return -1;
    }

    /*
     * Rebuild layout, repaint, composite, and blit.
     * The explicit call lets us change the output filename before flushing.
     */
    q_view_update(view);
    printf("Wrote %s\n", out_png);
    return 0;
}

/* ── main ─────────────────────────────────────────────────────────────────── */

int main(void)
{
    q_document_t    *doc;
    quanton_ctx_t    ctx;
    quanton_view_t   view;
    char            *html;
    lxb_dom_element_t *tbody;
    fb_entry_t       sorted[MAX_ENTRIES];

    /* 1. Scan the current directory */
    load_cwd_entries();

    /* 2. Initial sort: alphabetically by name */
    memcpy(sorted, g_entries, (size_t) g_nentries * sizeof(fb_entry_t));
    qsort(sorted, (size_t) g_nentries, sizeof(fb_entry_t), cmp_name);

    /* 3. Build the full HTML document */
    html = build_full_html(sorted, g_nentries);
    if (html == NULL) {
        fprintf(stderr, "OOM building full HTML\n");
        return 1;
    }

    /* 4. Parse HTML into a quanton document */
    doc = q_document_create();
    if (doc == NULL) {
        fprintf(stderr, "q_document_create failed\n");
        free(html);
        return 1;
    }
    if (q_document_load_html(doc, html, strlen(html), NULL) != 0) {
        fprintf(stderr, "q_document_load_html failed\n");
        free(html);
        q_document_destroy(doc);
        return 1;
    }
    free(html);

    /* 5. Set up context and view */
    memset(&ctx,  0, sizeof(ctx));
    memset(&view, 0, sizeof(view));
    ctx.backend     = &q_backend_png;
    view.ctx        = &ctx;
    view.document   = doc;
    view.vp_width   = VP_WIDTH;
    view.vp_height  = VP_HEIGHT;

    if (ctx.backend->create_window(&view, VP_WIDTH, VP_HEIGHT,
                                   "filebrowser_name.png") != 0) {
        fprintf(stderr, "create_window failed\n");
        q_document_destroy(doc);
        return 1;
    }

    /* 6. First render: name-sorted */
    q_dom_mark_dirty(&view, NULL,
                     (q_dirty_flags_t)(Q_DIRTY_LAYOUT | Q_DIRTY_PAINT));
    q_view_update(&view);
    printf("Wrote filebrowser_name.png\n");

    /*
     * 7. Locate the <tbody id="ftb"> element for subsequent innerHTML swaps.
     *    q_dom_get_element_by_id searches the lexbor DOM, which persists
     *    across relayouts — only the box tree is rebuilt by q_view_update().
     */
    tbody = q_dom_get_element_by_id(&view, "ftb");
    if (tbody == NULL) {
        fprintf(stderr, "Could not find #ftb element\n");
        goto done;
    }

    /* 8. Re-sort by size → filebrowser_size.png */
    if (render_sorted(&view, tbody, sorted, g_nentries,
                      cmp_size, "filebrowser_size.png") != 0) {
        goto done;
    }

    /* 9. Re-sort by modification date → filebrowser_date.png */
    render_sorted(&view, tbody, sorted, g_nentries,
                  cmp_mtime, "filebrowser_date.png");

done:
    ctx.backend->destroy_window(&view);
    free(view.framebuffer);
    q_layout_free_tree(view.layout_root);
    q_document_destroy(doc);
    return 0;
}
