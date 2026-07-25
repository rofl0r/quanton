/*
 * visual_inspect.c
 *
 * Headless, script-driven tool for visually inspecting quanton's rendering
 * and interactive (mouse/keyboard) behavior without a live display.
 *
 * It loads a static HTML file through the PNG backend, then replays a small
 * text script of synthetic input events (clicks, mouse wheel, key presses)
 * against the resulting view, letting each step be snapshotted to a PNG file
 * and/or the box tree dumped to stdout for inspection.
 *
 * This exists so that an agentic session (no attached X11/SDL2 display) can
 * still verify things like scrollbar visibility, form-widget caret/focus
 * behavior, or checkbox/radio "checked" rendering by rendering PNG frames
 * before/after simulated interaction and viewing them.
 *
 * See TOOLS.md for the script command reference and usage examples.
 *
 * Build: make visual_inspect
 * Usage: ./visual_inspect <html_file> <script_file> [width] [height]
 */

#define _POSIX_C_SOURCE 200809L

#include "quanton.h"

#include "lexbor/dom/interfaces/element.h"
#include "lexbor/dom/interfaces/node.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VI_DEFAULT_W 800
#define VI_DEFAULT_H 600
#define VI_LINE_MAX  1024

static char *read_whole_file(const char *path, size_t *out_len)
{
    FILE *fp;
    char *buf;
    long  size;

    fp = fopen(path, "rb");
    if (fp == NULL) {
        fprintf(stderr, "visual_inspect: cannot open '%s'\n", path);
        return NULL;
    }
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return NULL;
    }
    size = ftell(fp);
    if (size < 0 || fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return NULL;
    }
    buf = (char *) malloc((size_t) size + 1u);
    if (buf == NULL) {
        fprintf(stderr, "visual_inspect: out of memory reading '%s'\n", path);
        fclose(fp);
        return NULL;
    }
    if (size > 0 && fread(buf, 1, (size_t) size, fp) != (size_t) size) {
        free(buf);
        fclose(fp);
        return NULL;
    }
    buf[size] = '\0';
    fclose(fp);
    if (out_len != NULL) {
        *out_len = (size_t) size;
    }
    return buf;
}

/* Save the current view contents to `path`, forcing a fresh composite. */
static void render_to_png(quanton_view_t *view, const char *path)
{
    char *dup;

    if (view == NULL || path == NULL) {
        return;
    }

    dup = strdup(path);
    if (dup == NULL) {
        return;
    }
    free(view->window_handle);
    view->window_handle = dup;

    q_composite_frame(view);
    if (view->ctx != NULL && view->ctx->backend != NULL
        && view->ctx->backend->blit != NULL)
    {
        view->ctx->backend->blit(view);
    }
    printf("[render] %s\n", path);
}

static const char *q_box_type_name(int type)
{
    switch (type) {
    case Q_BOX_BLOCK:              return "block";
    case Q_BOX_IMAGE:              return "image";
    case Q_BOX_TEXT:               return "text";
    case Q_BOX_LINE_BREAK:         return "line-break";
    case Q_BOX_INLINE_CONTAINER:   return "inline-container";
    case Q_BOX_LINE:               return "line";
    case Q_BOX_TABLE:              return "table";
    case Q_BOX_TABLE_SECTION:      return "table-section";
    case Q_BOX_TABLE_ROW:          return "table-row";
    case Q_BOX_TABLE_CELL:         return "table-cell";
    case Q_BOX_TABLE_CAPTION:      return "table-caption";
    default:                       return "?";
    }
}

static const char *q_widget_type_name(int wt)
{
    switch (wt) {
    case Q_WIDGET_INPUT_TEXT:   return "input-text";
    case Q_WIDGET_INPUT_SUBMIT: return "input-submit";
    case Q_WIDGET_INPUT_CHECK:  return "checkbox";
    case Q_WIDGET_INPUT_RADIO:  return "radio";
    case Q_WIDGET_BUTTON:       return "button";
    case Q_WIDGET_SELECT:       return "select";
    case Q_WIDGET_TEXTAREA:     return "textarea";
    default:                    return "-";
    }
}

/* Recursively print a compact one-line-per-box tree dump. */
static void dump_box_tree(q_box_t *box, int depth)
{
    q_box_t *child;

    if (box == NULL) {
        return;
    }

    printf("%*s%s", depth * 2, "", q_box_type_name((int) box->type));
    if (box->widget_type != 0) {
        printf(" widget=%s", q_widget_type_name((int) box->widget_type));
        if (box->widget_type == Q_WIDGET_INPUT_CHECK
            || box->widget_type == Q_WIDGET_INPUT_RADIO)
        {
            printf(" checked=%d", box->widget_checked);
        }
        if (box->widget_type == Q_WIDGET_INPUT_TEXT
            || box->widget_type == Q_WIDGET_TEXTAREA)
        {
            printf(" value=\"%.*s\" caret=%zu focused=%d",
                   (int) box->widget_value_len,
                   (box->widget_value != NULL) ? box->widget_value : "",
                   box->widget_caret, box->widget_focused);
        }
    }
    printf(" x=%.0f y=%.0f w=%.0f h=%.0f\n",
           (double) box->x, (double) box->y,
           (double) box->width, (double) box->height);

    for (child = box->first_child; child != NULL; child = child->next_sibling) {
        dump_box_tree(child, depth + 1);
    }
}

static uint32_t key_name_to_sym(const char *name)
{
    if (strcmp(name, "left") == 0)      return Q_KEY_LEFT;
    if (strcmp(name, "right") == 0)     return Q_KEY_RIGHT;
    if (strcmp(name, "home") == 0)      return Q_KEY_HOME;
    if (strcmp(name, "end") == 0)       return Q_KEY_END;
    if (strcmp(name, "backspace") == 0) return Q_KEY_BACKSPACE;
    if (strcmp(name, "delete") == 0)    return Q_KEY_DELETE;
    if (strlen(name) == 1 && isprint((unsigned char) name[0])) {
        return (uint32_t) (unsigned char) name[0];
    }
    fprintf(stderr, "visual_inspect: unknown key name '%s'\n", name);
    return 0;
}

static void dispatch_mouse(quanton_view_t *view, q_event_type_t type,
                          int x, int y, int button)
{
    q_event_t ev;

    memset(&ev, 0, sizeof(ev));
    ev.type = type;
    ev.mouse_x = x;
    ev.mouse_y = y;
    ev.mouse_button = button;
    q_event_dispatch(view, &ev);
}

static void run_script(quanton_view_t *view, const char *script_path)
{
    FILE *fp;
    char  line[VI_LINE_MAX];

    fp = fopen(script_path, "r");
    if (fp == NULL) {
        fprintf(stderr, "visual_inspect: cannot open script '%s'\n", script_path);
        return;
    }

    while (fgets(line, sizeof(line), fp) != NULL) {
        char *cmd;
        char *saveptr = NULL;

        /* Strip trailing newline and comments. */
        line[strcspn(line, "\r\n")] = '\0';
        {
            char *hash = strchr(line, '#');
            if (hash != NULL) {
                *hash = '\0';
            }
        }

        cmd = strtok_r(line, " \t", &saveptr);
        if (cmd == NULL || cmd[0] == '\0') {
            continue;
        }

        if (strcmp(cmd, "render") == 0) {
            char *path = strtok_r(NULL, " \t", &saveptr);
            if (path != NULL) {
                render_to_png(view, path);
            }
        } else if (strcmp(cmd, "dump") == 0) {
            printf("[dump]\n");
            dump_box_tree(view->layout_root, 0);
        } else if (strcmp(cmd, "click") == 0 || strcmp(cmd, "down") == 0
                   || strcmp(cmd, "up") == 0 || strcmp(cmd, "move") == 0)
        {
            char *xs = strtok_r(NULL, " \t", &saveptr);
            char *ys = strtok_r(NULL, " \t", &saveptr);
            int x = (xs != NULL) ? atoi(xs) : 0;
            int y = (ys != NULL) ? atoi(ys) : 0;

            if (strcmp(cmd, "click") == 0) {
                dispatch_mouse(view, Q_EVENT_MOUSE_DOWN, x, y, 0);
                dispatch_mouse(view, Q_EVENT_MOUSE_UP, x, y, 0);
                dispatch_mouse(view, Q_EVENT_MOUSE_CLICK, x, y, 0);
            } else if (strcmp(cmd, "down") == 0) {
                dispatch_mouse(view, Q_EVENT_MOUSE_DOWN, x, y, 0);
            } else if (strcmp(cmd, "up") == 0) {
                dispatch_mouse(view, Q_EVENT_MOUSE_UP, x, y, 0);
            } else {
                dispatch_mouse(view, Q_EVENT_MOUSE_MOVE, x, y, 0);
            }
        } else if (strcmp(cmd, "wheel") == 0) {
            char *ds = strtok_r(NULL, " \t", &saveptr);
            char *xs = strtok_r(NULL, " \t", &saveptr);
            char *ys = strtok_r(NULL, " \t", &saveptr);
            q_event_t ev;

            memset(&ev, 0, sizeof(ev));
            ev.type = Q_EVENT_MOUSE_WHEEL;
            ev.wheel_delta = (ds != NULL) ? atoi(ds) : 0;
            ev.mouse_x = (xs != NULL) ? atoi(xs) : 0;
            ev.mouse_y = (ys != NULL) ? atoi(ys) : 0;
            q_event_dispatch(view, &ev);
        } else if (strcmp(cmd, "key") == 0) {
            char *name = strtok_r(NULL, " \t", &saveptr);
            if (name != NULL) {
                uint32_t sym = key_name_to_sym(name);
                q_event_t ev;

                memset(&ev, 0, sizeof(ev));
                ev.type = Q_EVENT_KEY_DOWN;
                ev.key_sym = sym;
                q_event_dispatch(view, &ev);
            }
        } else {
            fprintf(stderr, "visual_inspect: unknown command '%s'\n", cmd);
        }
    }

    fclose(fp);
}

int main(int argc, char **argv)
{
    q_document_t  *doc;
    quanton_ctx_t  ctx;
    quanton_view_t view;
    char          *html;
    size_t         html_len = 0;
    int            width = VI_DEFAULT_W;
    int            height = VI_DEFAULT_H;

    if (argc < 3) {
        fprintf(stderr,
                "usage: %s <html_file> <script_file> [width] [height]\n",
                argv[0]);
        return 1;
    }
    if (argc >= 4) width  = atoi(argv[3]);
    if (argc >= 5) height = atoi(argv[4]);

    html = read_whole_file(argv[1], &html_len);
    if (html == NULL) {
        return 1;
    }

    doc = q_document_create();
    if (doc == NULL) {
        free(html);
        return 1;
    }
    if (q_document_load_html(doc, html, html_len, NULL) != 0) {
        fprintf(stderr, "visual_inspect: failed to parse '%s'\n", argv[1]);
        free(html);
        q_document_destroy(doc);
        return 1;
    }
    free(html);

    memset(&ctx, 0, sizeof(ctx));
    memset(&view, 0, sizeof(view));
    ctx.backend = &q_backend_png;
    view.ctx = &ctx;
    view.document = doc;
    view.vp_width = width;
    view.vp_height = height;

    if (ctx.backend->create_window(&view, width, height, "visual_inspect_initial.png") != 0) {
        q_document_destroy(doc);
        return 1;
    }

    q_dom_mark_dirty(&view, NULL,
                     (q_dirty_flags_t) (Q_DIRTY_LAYOUT | Q_DIRTY_PAINT));
    q_view_update(&view);

    run_script(&view, argv[2]);

    ctx.backend->destroy_window(&view);
    q_layout_free_tree(view.layout_root);
    q_document_destroy(doc);
    return 0;
}
