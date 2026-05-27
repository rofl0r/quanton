#define _POSIX_C_SOURCE 200809L
#include "quanton.h"

#include "lexbor/dom/interface.h"
#include "lexbor/dom/interfaces/element.h"
#include "lexbor/dom/interfaces/node.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define FLOAT_TOLERANCE 0.01f
#define TEST_WIDTH 800
#define TEST_HEIGHT 600
#define Q_BORDER_RGB 48u
#define Q_BACKGROUND_RGB 242u
/* Sample a known non-text area inside the first block. */
#define Q_NON_TEXT_SAMPLE_X 200
#define Q_NON_TEXT_SAMPLE_Y 10

#if defined(QUANTON_BACKEND_X11) || defined(QUANTON_BACKEND_SDL2)
static char *make_url_from_filename(const char *filename)
{
    size_t len;
    char *url;

    if (filename == NULL || filename[0] == '\0') {
        return NULL;
    }

    if (strstr(filename, "://") != NULL) {
        return strdup(filename);
    }

    len = strlen(filename);
    /* Relative paths that don't start with "./" need it inserted so that
     * q_resource_parse_file_url() accepts them (it requires "./" or "/").
     * Note: len > 0 is guaranteed by the empty-string check above, so
     * reading filename[1] is safe (it may be '\0' but never out of bounds). */
    {
        int is_absolute      = (filename[0] == '/');
        int has_dot_slash    = (filename[0] == '.' && filename[1] == '/');
        int need_dot_slash   = (!is_absolute && !has_dot_slash);
        size_t extra = need_dot_slash ? 2u : 0u; /* "./" is 2 chars */
        url = (char *) malloc(sizeof("file://") + extra + len);
        if (url == NULL) {
            return NULL;
        }
        memcpy(url, "file://", sizeof("file://") - 1);
        if (need_dot_slash) {
            memcpy(url + sizeof("file://") - 1, "./", 2);
        }
        memcpy(url + sizeof("file://") - 1 + extra, filename, len + 1);
    }
    return url;
}
#endif

static int g_event_called;
static q_box_t *g_event_target_box;
static lxb_dom_node_t *g_event_target;

static int nearly_equal(float a, float b)
{
    return fabsf(a - b) < FLOAT_TOLERANCE;
}

static void assert_pixel_rgba(const uint8_t *pixels, int width, int height, int x, int y,
                              uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    size_t idx;

    assert(pixels != NULL);
    assert(width > 0);
    assert(x >= 0);
    assert(y >= 0);
    assert(x < width);
    assert(y < height);

    idx = (size_t) (y * width + x) * 4u;
    assert(pixels[idx + 0] == r);
    assert(pixels[idx + 1] == g);
    assert(pixels[idx + 2] == b);
    assert(pixels[idx + 3] == a);
}

#if defined(QUANTON_BACKEND_PNG) || defined(QUANTON_BACKEND_X11) || defined(QUANTON_BACKEND_SDL2)
static int framebuffer_has_ink(const uint8_t *pixels, int width, int height)
{
    size_t i;
    size_t n;

    if (pixels == NULL || width <= 0 || height <= 0) {
        return 0;
    }

        n = (size_t) width * (size_t) height;
        for (i = 0; i < n; ++i) {
            size_t idx = i * 4u;
            if (pixels[idx + 0] != 255u
                || pixels[idx + 1] != 255u
                || pixels[idx + 2] != 255u)
            {
                return 1;
            }
        }

        return 0;
    }
    #endif

    #if defined(QUANTON_BACKEND_PNG)
    static int framebuffer_has_text_shades(const uint8_t *pixels, int width, int height)
    {
        size_t i;
        size_t n;

        if (pixels == NULL || width <= 0 || height <= 0) {
            return 0;
        }

        n = (size_t) width * (size_t) height;
        for (i = 0; i < n; ++i) {
            size_t idx = i * 4u;
            uint8_t r = pixels[idx + 0];
            uint8_t g = pixels[idx + 1];
            uint8_t b = pixels[idx + 2];

            if (r == g && g == b && r > Q_BORDER_RGB && r < Q_BACKGROUND_RGB) {
                return 1;
            }
        }

        return 0;
    }
    #endif

    #if defined(QUANTON_BACKEND_PNG) || defined(QUANTON_BACKEND_X11) || defined(QUANTON_BACKEND_SDL2)
    static void backend_event_handler(quanton_view_t *view, const q_event_t *event, void *userdata)
{
    (void) userdata;

    if (view == NULL || event == NULL || view->ctx == NULL || view->ctx->backend == NULL) {
        return;
    }

    if (event->type == Q_EVENT_CLOSE) {
        view->should_close = 1;
        return;
    }

    if (event->type == Q_EVENT_RESIZE) {
        q_composite_frame(view);
        view->ctx->backend->blit(view);
    }
}
#endif

static void capture_event_handler(quanton_view_t *view, const q_event_t *event, void *userdata)
{
    (void) view;
    (void) userdata;

    if (event == NULL) {
        return;
    }

    g_event_called = 1;
    g_event_target_box = event->target_box;
    g_event_target = event->target;
}

#if defined(QUANTON_BACKEND_PNG)
static void render_html_case_to_png(const char *html_url, const char *output_png, int width, int height)
{
    q_document_t *doc;
    q_box_t *root;
    quanton_ctx_t ctx;
    quanton_view_t view;
    FILE *fp;

    doc = q_document_create();
    assert(doc != NULL);
    assert(q_document_load_url(doc, html_url) == 0);

    root = q_layout_build_tree(doc);
    assert(root != NULL);

    q_layout_measure(root, (float) width, 0.0f);
    q_layout_position(root, 0.0f, 0.0f);
    q_layout_position_absolute(root);
    q_paint_box(root);

    memset(&ctx, 0, sizeof(ctx));
    memset(&view, 0, sizeof(view));
    ctx.backend = &q_backend_png;
    view.ctx = &ctx;
    view.layout_root = root;

    assert(ctx.backend->create_window(&view, width, height, output_png) == 0);
    q_composite_frame(&view);
    assert(framebuffer_has_ink(view.framebuffer, view.vp_width, view.vp_height));
    assert(framebuffer_has_text_shades(view.framebuffer, view.vp_width, view.vp_height));
    ctx.backend->blit(&view);

    fp = fopen(output_png, "rb");
    assert(fp != NULL);
    fclose(fp);

    free(view.framebuffer);
    view.framebuffer = NULL;
    ctx.backend->destroy_window(&view);
    q_layout_free_tree(root);
    q_document_destroy(doc);
}
#endif

int main(int argc, char **argv)
{
    static const char html[] =
        "<html><body><div data-hit='1'>Hello</div><div><p>world</p></div></body></html>";
    q_document_t *doc;
    q_box_t *root;
    q_box_t *first_block;
    q_box_t *second_block;
    q_box_t *first_ic;
    q_box_t *first_line;
    q_box_t *first_text;
    q_font_cache_t *cache;
    q_font_t *font;
    q_shaped_run_t *run;
    uint8_t *buf;
    size_t len = 0;
#if defined(QUANTON_BACKEND_X11) || defined(QUANTON_BACKEND_SDL2)
    const char *interactive_html_file = NULL;
#else
    (void) argc;
    (void) argv;
#endif

#if defined(QUANTON_BACKEND_X11) || defined(QUANTON_BACKEND_SDL2)
    if (argc > 1) {
        interactive_html_file = argv[1];
    }
#endif

    buf = q_resource_load("file://./IMPLEMENTATION_PLAN_STAGE1_DONE.md", &len);
    assert(buf != NULL);
    assert(len > 0);
    q_resource_free(buf);

    doc = q_document_create();
    assert(doc != NULL);

    assert(q_document_load_html(doc, html, sizeof(html) - 1, "file://./tests/input.html")
           == 0);

    root = q_layout_build_tree(doc);
    assert(root != NULL);
    assert(root->first_child != NULL);

    q_layout_measure(root, 320.0f, 0.0f);
    q_layout_position(root, 10.0f, 20.0f);

    assert(nearly_equal(root->width, 320.0f));
    assert(nearly_equal(root->x, 10.0f));
    assert(nearly_equal(root->y, 20.0f));
    assert(root->height > 0.0f);

    first_block = root->first_child;
    second_block = first_block->next_sibling;
    assert(first_block != NULL);
    assert(second_block != NULL);
    assert(nearly_equal(first_block->x, 10.0f));
    assert(nearly_equal(first_block->y, 20.0f));
    assert(nearly_equal(first_block->width, 320.0f));
    assert(first_block->height > 0.0f);
    assert(nearly_equal(second_block->y, first_block->y + first_block->height));

    first_ic = first_block->first_child;
    assert(first_ic != NULL);
    assert(first_ic->type == Q_BOX_INLINE_CONTAINER);
    assert(first_ic->height > 0.0f);

    first_line = first_ic->first_child;
    assert(first_line != NULL);
    assert(first_line->type == Q_BOX_LINE);
    assert(first_line->height > 0.0f);

    first_text = first_line->first_child;
    assert(first_text != NULL);
    assert(first_text->type == Q_BOX_TEXT);
    assert(first_text->width > 0.0f);
    assert(first_text->height > 0.0f);
    if (first_text->run != NULL) {
        assert(first_text->run->count > 0);
    }
    assert(q_hit_test(root, 11, 21) == first_text);
    assert(q_event_find_delegate(first_text->dom_node, "data-hit") == first_block->dom_node);

    {
        quanton_view_t ev_view;
        q_event_t ev;

        memset(&ev_view, 0, sizeof(ev_view));
        memset(&ev, 0, sizeof(ev));
        ev_view.layout_root = root;
        ev_view.on_event = capture_event_handler;

        g_event_called = 0;
        g_event_target_box = NULL;
        g_event_target = NULL;

        ev.type = Q_EVENT_MOUSE_MOVE;
        ev.mouse_x = 11;
        ev.mouse_y = 21;
        q_event_dispatch(&ev_view, &ev);

        assert(g_event_called == 1);
        assert(g_event_target_box == first_text);
        assert(g_event_target == first_text->dom_node);
    }

    first_block->background_color = 0x102030FFu;
    first_block->border_width[0] = 2.0f;
    first_block->border_width[1] = 2.0f;
    first_block->border_width[2] = 2.0f;
    first_block->border_width[3] = 2.0f;
    first_block->border_color[0] = 0xAA0000FFu;
    first_block->border_color[1] = 0x00AA00FFu;
    first_block->border_color[2] = 0x0000AAFFu;
    first_block->border_color[3] = 0xAAAA00FFu;

    q_paint_box(root);

    assert(root->tile != NULL);
    assert(root->tile_w > 0);
    assert(root->tile_h > 0);
    assert(first_block->tile != NULL);
    assert(first_block->tile_w > 4);
    assert(first_block->tile_h > 4);

    assert_pixel_rgba(first_block->tile, first_block->tile_w, first_block->tile_h, 3, 0, 170, 0, 0, 255);
    assert_pixel_rgba(first_block->tile, first_block->tile_w, first_block->tile_h, first_block->tile_w - 1, 3, 0, 170, 0, 255);
    assert_pixel_rgba(first_block->tile, first_block->tile_w, first_block->tile_h, 3, first_block->tile_h - 1, 0, 0, 170, 255);
    assert_pixel_rgba(first_block->tile, first_block->tile_w, first_block->tile_h, 0, 3, 170, 170, 0, 255);
    assert_pixel_rgba(first_block->tile, first_block->tile_w, first_block->tile_h,
                      Q_NON_TEXT_SAMPLE_X, Q_NON_TEXT_SAMPLE_Y, 16, 32, 48, 255);

    assert_pixel_rgba(root->tile, root->tile_w, root->tile_h,
                      Q_NON_TEXT_SAMPLE_X, Q_NON_TEXT_SAMPLE_Y, 16, 32, 48, 255);

#if defined(QUANTON_BACKEND_PNG) || defined(QUANTON_BACKEND_X11) || defined(QUANTON_BACKEND_SDL2)
    {
        static const char flex_html[] =
            "<html><body><div style='display:flex'><div>one</div><div>two</div><div>three</div></div></body></html>";
        q_document_t *flex_doc;
        q_box_t *flex_root;
        q_box_t *flex_container;
        q_box_t *flex_a;
        q_box_t *flex_b;
        q_box_t *flex_c;

        flex_doc = q_document_create();
        assert(flex_doc != NULL);
        assert(q_document_load_html(flex_doc, flex_html, sizeof(flex_html) - 1,
                                    "file://./tests/flex.html") == 0);

        flex_root = q_layout_build_tree(flex_doc);
        assert(flex_root != NULL);
        q_layout_measure(flex_root, 300.0f, 0.0f);
        q_layout_position(flex_root, 0.0f, 0.0f);

        flex_container = flex_root->first_child;
        assert(flex_container != NULL);
        assert(flex_container->is_flex_container == 1);

        flex_a = flex_container->first_child;
        assert(flex_a != NULL);
        flex_b = flex_a->next_sibling;
        assert(flex_b != NULL);
        flex_c = flex_b->next_sibling;
        assert(flex_c != NULL);
        assert(flex_c->next_sibling == NULL);

        assert(nearly_equal(flex_a->width, 100.0f));
        assert(nearly_equal(flex_b->width, 100.0f));
        assert(nearly_equal(flex_c->width, 100.0f));
        assert(nearly_equal(flex_b->x, flex_a->x + flex_a->width));
        assert(nearly_equal(flex_c->x, flex_b->x + flex_b->width));
        assert(nearly_equal(flex_a->y, flex_container->y));
        assert(nearly_equal(flex_b->y, flex_container->y));
        assert(nearly_equal(flex_c->y, flex_container->y));
        assert(nearly_equal(flex_container->height, flex_a->height));
        assert(nearly_equal(flex_container->height, flex_b->height));
        assert(nearly_equal(flex_container->height, flex_c->height));

        q_layout_free_tree(flex_root);
        q_document_destroy(flex_doc);
    }

    /* ── Absolute / fixed positioning ──────────────────────────────────── */
    {
        /* relative container 400x200; absolute child at left:10, top:20, 100x40 */
        static const char abs_html[] =
            "<html><body>"
            "<div style='position:relative; width:400px; height:200px;'>"
            "<div style='position:absolute; left:10px; top:20px; width:100px; height:40px;'>Absolute</div>"
            "<div>Normal flow</div>"
            "</div>"
            "</body></html>";
        q_document_t *abs_doc;
        q_box_t *abs_root;
        q_box_t *abs_container;
        q_box_t *abs_child;
        q_box_t *normal_child;

        abs_doc = q_document_create();
        assert(abs_doc != NULL);
        assert(q_document_load_html(abs_doc, abs_html, sizeof(abs_html) - 1,
                                    "file://./tests/abs.html") == 0);

        abs_root = q_layout_build_tree(abs_doc);
        assert(abs_root != NULL);
        q_layout_measure(abs_root, 800.0f, 0.0f);
        q_layout_position(abs_root, 0.0f, 0.0f);
        q_layout_position_absolute(abs_root);

        abs_container = abs_root->first_child;
        assert(abs_container != NULL);
        assert(abs_container->position == Q_POSITION_RELATIVE);
        assert(nearly_equal(abs_container->width,  400.0f));
        assert(nearly_equal(abs_container->height, 200.0f));

        abs_child = abs_container->first_child;
        assert(abs_child != NULL);
        assert(abs_child->position == Q_POSITION_ABSOLUTE);
        assert(nearly_equal(abs_child->x, abs_container->x + 10.0f));
        assert(nearly_equal(abs_child->y, abs_container->y + 20.0f));
        assert(nearly_equal(abs_child->width,  100.0f));
        assert(nearly_equal(abs_child->height,  40.0f));

        /* Normal-flow sibling should start at the container top (abs child
         * is out of flow and contributes zero height to normal flow). */
        normal_child = abs_child->next_sibling;
        assert(normal_child != NULL);
        assert(nearly_equal(normal_child->y, abs_container->y));

        q_layout_free_tree(abs_root);
        q_document_destroy(abs_doc);
    }

    /* ── z-index stacking order ────────────────────────────────────────── */
    {
        q_document_t *z_doc;
        q_box_t *z_root;
        q_box_t *z_container;
        q_box_t *z_low;
        q_box_t *z_high;
        z_doc = q_document_create();
        assert(z_doc != NULL);
        assert(z_doc != NULL);
        assert(q_document_load_url(z_doc, "file://./tests/html/z_index_stack.html") == 0);

        z_root = q_layout_build_tree(z_doc);
        assert(z_root != NULL);
        q_layout_measure(z_root, 200.0f, 0.0f);
        q_layout_position(z_root, 0.0f, 0.0f);
        q_layout_position_absolute(z_root);

        z_container = z_root->first_child;
        assert(z_container != NULL);
        z_low = z_container->first_child;
        assert(z_low != NULL);
        z_high = z_low->next_sibling;
        assert(z_high != NULL);
        assert(z_high->next_sibling == NULL);
        assert(z_low->has_z_index == 1);
        assert(z_high->has_z_index == 1);
        assert(z_low->z_index == 1);
        assert(z_high->z_index == 5);

        z_low->background_color = 0xFF0000FFu;
        z_high->background_color = 0x0000FFFFu;

        q_paint_box(z_root);
        assert(z_root->tile != NULL);
        /* overlap point at (80,80) should be blue from higher z-index box */
        assert_pixel_rgba(z_root->tile, z_root->tile_w, z_root->tile_h, 80, 80, 0, 0, 255, 255);

        q_layout_free_tree(z_root);
        q_document_destroy(z_doc);
    }

    {
        quanton_ctx_t bctx;
        quanton_view_t bview;

        memset(&bctx, 0, sizeof(bctx));
        memset(&bview, 0, sizeof(bview));

#if defined(QUANTON_BACKEND_PNG)
        bctx.backend = &q_backend_png;
#elif defined(QUANTON_BACKEND_X11)
        bctx.backend = &q_backend_x11;
#elif defined(QUANTON_BACKEND_SDL2)
        bctx.backend = &q_backend_sdl2;
#endif

        bview.ctx = &bctx;
        bview.layout_root = root;
        bview.vp_width = TEST_WIDTH;
        bview.vp_height = TEST_HEIGHT;
        bview.on_event = backend_event_handler;

#if defined(QUANTON_BACKEND_PNG)
        assert(bctx.backend->create_window(&bview, TEST_WIDTH, TEST_HEIGHT, "output.png") == 0);
        q_composite_frame(&bview);
        assert(bview.framebuffer != NULL);
        assert(framebuffer_has_ink(bview.framebuffer, bview.vp_width, bview.vp_height));
        bctx.backend->blit(&bview);
        {
            FILE *fp = fopen("output.png", "rb");
            assert(fp != NULL);
            fclose(fp);
        }
        free(bview.framebuffer);
        bview.framebuffer = NULL;
        bctx.backend->destroy_window(&bview);

        render_html_case_to_png("file://./tests/html/basic_blocks.html", "output_basic_blocks.png", TEST_WIDTH, TEST_HEIGHT);
        render_html_case_to_png("file://./tests/html/inline_wrap.html", "output_inline_wrap.png", TEST_WIDTH, TEST_HEIGHT);
        render_html_case_to_png("file://./tests/html/nested_blocks.html", "output_nested_blocks.png", TEST_WIDTH, TEST_HEIGHT);
        render_html_case_to_png("file://./tests/html/flex_row.html", "output_flex_row.png", TEST_WIDTH, TEST_HEIGHT);
        render_html_case_to_png("file://./tests/html/absolute_pos.html", "output_absolute_pos.png", TEST_WIDTH, TEST_HEIGHT);
        render_html_case_to_png("file://./tests/html/z_index_stack.html", "output_z_index_stack.png", TEST_WIDTH, TEST_HEIGHT);
#else
        q_document_t *interactive_doc = NULL;
        q_box_t *interactive_root = root;
        char *interactive_url = NULL;

        if (interactive_html_file != NULL) {
            interactive_url = make_url_from_filename(interactive_html_file);
            assert(interactive_url != NULL);

            interactive_doc = q_document_create();
            assert(interactive_doc != NULL);
            assert(q_document_load_url(interactive_doc, interactive_url) == 0);

            interactive_root = q_layout_build_tree(interactive_doc);
            assert(interactive_root != NULL);
            q_layout_measure(interactive_root, (float) TEST_WIDTH, 0.0f);
            q_layout_position(interactive_root, 0.0f, 0.0f);
            q_paint_box(interactive_root);
        }

        bview.layout_root = interactive_root;
        if (bctx.backend->create_window(&bview, TEST_WIDTH, TEST_HEIGHT, "quanton-test") == 0) {
            q_composite_frame(&bview);
            bctx.backend->blit(&bview);
            while (!bview.should_close) {
                struct timespec ts;
                ts.tv_sec = 0;
                ts.tv_nsec = 16L * 1000L * 1000L;
                bctx.backend->poll_events(&bview);
                nanosleep(&ts, NULL);
            }
            bctx.backend->destroy_window(&bview);
        }
        if (interactive_root != root) {
            q_layout_free_tree(interactive_root);
        }
        q_document_destroy(interactive_doc);
        free(interactive_url);
#endif
    }
#endif

    q_layout_free_tree(root);

    /* ── DOM mutation API + dirty tracking (step 11) ─────────────────────── */
    {
        static const char mut_html[] =
            "<html><body>"
            "<div id='box' class='alpha'>Hello</div>"
            "</body></html>";
        q_document_t    *mut_doc;
        q_box_t         *mut_root;
        q_box_t         *mut_block;
        float            orig_height;
        quanton_view_t   mut_view;
        lxb_dom_element_t *found;

        mut_doc = q_document_create();
        assert(mut_doc != NULL);
        assert(q_document_load_html(mut_doc, mut_html, sizeof(mut_html) - 1,
                                    "file://./tests/mut.html") == 0);

        mut_root = q_layout_build_tree(mut_doc);
        assert(mut_root != NULL);
        q_layout_measure(mut_root, 400.0f, 0.0f);
        q_layout_position(mut_root, 0.0f, 0.0f);

        mut_block = mut_root->first_child;
        assert(mut_block != NULL);
        orig_height = mut_block->height;
        assert(orig_height > 0.0f);

        /* Set up a minimal view for q_view_update */
        memset(&mut_view, 0, sizeof(mut_view));
        mut_view.document    = mut_doc;
        mut_view.layout_root = mut_root;
        mut_view.vp_width    = 400;
        mut_view.vp_height   = 300;

        /* ── q_dom_has_class ── */
        assert(q_dom_has_class(lxb_dom_interface_element(mut_block->dom_node),
                               "alpha") == true);
        assert(q_dom_has_class(lxb_dom_interface_element(mut_block->dom_node),
                               "beta") == false);

        /* ── q_dom_add_class ── */
        q_dom_add_class(&mut_view,
                        lxb_dom_interface_element(mut_block->dom_node),
                        "beta");
        assert(q_dom_has_class(lxb_dom_interface_element(mut_block->dom_node),
                               "alpha") == true);
        assert(q_dom_has_class(lxb_dom_interface_element(mut_block->dom_node),
                               "beta") == true);
        /* adding the same class again is a no-op */
        q_dom_add_class(&mut_view,
                        lxb_dom_interface_element(mut_block->dom_node),
                        "beta");
        assert(q_dom_has_class(lxb_dom_interface_element(mut_block->dom_node),
                               "beta") == true);

        /* ── q_dom_remove_class ── */
        q_dom_remove_class(&mut_view,
                           lxb_dom_interface_element(mut_block->dom_node),
                           "alpha");
        assert(q_dom_has_class(lxb_dom_interface_element(mut_block->dom_node),
                               "alpha") == false);
        assert(q_dom_has_class(lxb_dom_interface_element(mut_block->dom_node),
                               "beta") == true);

        /* ── q_dom_set_attr / q_dom_remove_attr ── */
        assert(q_dom_set_attr(&mut_view,
                              lxb_dom_interface_element(mut_block->dom_node),
                              "data-test", "42") == 0);
        assert(q_dom_remove_attr(&mut_view,
                                 lxb_dom_interface_element(mut_block->dom_node),
                                 "data-test") == 0);

        /* ── q_dom_mark_dirty accumulates flags ── */
        mut_view.dirty_flags = 0;
        q_dom_mark_dirty(&mut_view, mut_block->dom_node, Q_DIRTY_LAYOUT);
        assert((mut_view.dirty_flags & Q_DIRTY_LAYOUT) != 0);
        q_dom_mark_dirty(&mut_view, NULL, Q_DIRTY_PAINT);
        assert((mut_view.dirty_flags & Q_DIRTY_PAINT) != 0);

        /* ── q_view_update triggers full relayout ── */
        assert(q_dom_set_text_content(
                    &mut_view,
                    lxb_dom_interface_element(mut_block->dom_node),
                    "Updated text content", 20) == 0);
        assert((mut_view.dirty_flags & Q_DIRTY_LAYOUT) != 0);

        q_view_update(&mut_view);

        /* After update dirty flags should be cleared */
        assert(mut_view.dirty_flags == 0);

        /* A new layout_root should have been built */
        assert(mut_view.layout_root != NULL);
        /* old mut_root was freed; new root was built from same doc */
        mut_root = mut_view.layout_root;

        /* ── q_dom_query_selector ── */
        found = q_dom_query_selector(&mut_view, "div");
        assert(found != NULL);
        found = q_dom_query_selector(&mut_view, "span");
        assert(found == NULL); /* no span in the document */

        /* ── q_dom_append_element + relayout ── */
        {
            lxb_dom_element_t *body_el;
            lxb_dom_element_t *new_el;

            body_el = q_dom_query_selector(&mut_view, "body");
            assert(body_el != NULL);
            new_el = q_dom_append_element(&mut_view, body_el, "p");
            assert(new_el != NULL);
            assert((mut_view.dirty_flags & Q_DIRTY_LAYOUT) != 0);
            q_view_update(&mut_view);
            assert(mut_view.dirty_flags == 0);
            assert(mut_view.layout_root != NULL);
        }

        /* ── q_dom_remove_node + relayout ── */
        {
            lxb_dom_element_t *div_el;

            div_el = q_dom_query_selector(&mut_view, "div");
            assert(div_el != NULL);
            assert(q_dom_remove_node(&mut_view,
                                     lxb_dom_interface_node(div_el)) == 0);
            assert((mut_view.dirty_flags & Q_DIRTY_LAYOUT) != 0);
            q_view_update(&mut_view);
            assert(mut_view.dirty_flags == 0);
            assert(mut_view.layout_root != NULL);
        }

        /* ── q_view_refresh forces full relayout even when clean ── */
        {
            assert(mut_view.dirty_flags == 0);
            q_view_refresh(&mut_view);
            assert(mut_view.dirty_flags == 0);
            assert(mut_view.layout_root != NULL);
        }

        q_layout_free_tree(mut_view.layout_root);
        q_document_destroy(mut_doc);
    }

    cache = q_font_cache_create();
    assert(cache != NULL);

    font = q_font_load(cache, "sans-serif", NULL, 16.0f, 400);
    if (font != NULL) {
        assert(q_font_measure(font, "hello", 5) > 0.0f);

        run = q_font_shape_run(font, "hello", 5);
        assert(run != NULL);
        assert(run->count == 5);
        q_shaped_run_free(run);
    }

    q_font_cache_destroy(cache);
    q_document_destroy(doc);

    puts("ok");
    return 0;
}
