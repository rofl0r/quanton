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
#include <unistd.h>

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

static void write_test_png_file(const char *path)
{
    static const uint8_t png_bytes[] = {
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A,
        0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52,
        0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
        0x08, 0x06, 0x00, 0x00, 0x00, 0x1F, 0x15, 0xC4,
        0x89, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x44, 0x41,
        0x54, 0x78, 0x9C, 0x63, 0x10, 0x32, 0x09, 0xFB,
        0x0F, 0x00, 0x02, 0x94, 0x01, 0x9C, 0x1D, 0x5B,
        0x46, 0x5F, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45,
        0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82
    };
    FILE *fp;

    fp = fopen(path, "wb");
    assert(fp != NULL);
    assert(fwrite(png_bytes, 1, sizeof(png_bytes), fp) == sizeof(png_bytes));
    fclose(fp);
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
    /* Some cases are intentionally non-text-focused (e.g. image/overflow
     * fixtures), so PNG smoke coverage checks for rendered ink only. */
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

    {
        char *resolved;

        resolved = q_url_resolve("file://./tests/html/input.html", "../images/pixel.png");
        assert(resolved != NULL);
        assert(strcmp(resolved, "file://./tests/html/../images/pixel.png") == 0);
        free(resolved);

        resolved = q_url_resolve("file:///tmp/quanton/input.html", "pixel.png");
        assert(resolved != NULL);
        assert(strcmp(resolved, "file:///tmp/quanton/pixel.png") == 0);
        free(resolved);

        resolved = q_url_resolve("file://./tests/html/input.html", "/tmp/quanton/pixel.png");
        assert(resolved != NULL);
        assert(strcmp(resolved, "file:///tmp/quanton/pixel.png") == 0);
        free(resolved);

        resolved = q_url_resolve(NULL, "../pixel.png");
        assert(resolved != NULL);
        assert(strcmp(resolved, "file://./../pixel.png") == 0);
        free(resolved);
    }

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

    {
        static const char img_html[] =
            "<html><body><img src='quanton_test_image.png'></body></html>";
        static const char img_path[] = "/tmp/quanton_test_image.png";
        q_document_t *img_doc;
        q_box_t *img_root;
        q_box_t *img_ic;
        q_box_t *img_line;
        q_box_t *img_box;
        q_image_t *cached_a;
        q_image_t *cached_b;

        write_test_png_file(img_path);

        cached_a = q_image_load_url("file:///tmp/quanton_test_image.png");
        assert(cached_a != NULL);
        cached_b = q_image_load_url("file:///tmp/quanton_test_image.png");
        assert(cached_b == cached_a);
        assert(q_image_width(cached_a) == 1);
        assert(q_image_height(cached_a) == 1);
        q_image_release(cached_b);
        q_image_release(cached_a);

        img_doc = q_document_create();
        assert(img_doc != NULL);
        assert(q_document_load_html(img_doc, img_html, sizeof(img_html) - 1,
                                    "file:///tmp/quanton_test_doc.html") == 0);

        img_root = q_layout_build_tree(img_doc);
        assert(img_root != NULL);
        img_ic = img_root->first_child;
        assert(img_ic != NULL);
        assert(img_ic->type == Q_BOX_INLINE_CONTAINER);

        q_layout_measure(img_root, 100.0f, 0.0f);
        q_layout_position(img_root, 0.0f, 0.0f);

        img_line = img_ic->first_child;
        assert(img_line != NULL);
        assert(img_line->type == Q_BOX_LINE);
        img_box = img_line->first_child;
        assert(img_box != NULL);
        assert(img_box->type == Q_BOX_IMAGE);
        assert(img_box->image != NULL);
        assert(nearly_equal(img_box->width, 1.0f));
        assert(nearly_equal(img_box->height, 1.0f));

        q_paint_box(img_root);
        assert(img_box->tile != NULL);
        assert_pixel_rgba(img_box->tile, img_box->tile_w, img_box->tile_h, 0, 0,
                          0x12, 0x34, 0x56, 0xFF);
        assert_pixel_rgba(img_root->tile, img_root->tile_w, img_root->tile_h, 0, 0,
                          0x12, 0x34, 0x56, 0xFF);

        q_layout_free_tree(img_root);
        q_document_destroy(img_doc);
        assert(unlink(img_path) == 0);
    }

    /* ── Multi-format image loading (PNG / JPEG / GIF) ──────────────────── */
    {
        q_image_t *img;

        /* PNG: load tests/images/icon_red.png (16×16, lossless) */
        img = q_image_load_url("file://./tests/images/icon_red.png");
        assert(img != NULL);
        assert(q_image_width(img)  == 16);
        assert(q_image_height(img) == 16);
        assert(q_image_pixels(img) != NULL);
        /* corner pixel: R=220 G=50 B=50 A=255 */
        assert(q_image_pixels(img)[0] == 220u);
        assert(q_image_pixels(img)[1] ==  50u);
        assert(q_image_pixels(img)[2] ==  50u);
        assert(q_image_pixels(img)[3] == 255u);
        /* inner pixel at (8,8): R=200 G=30 B=30 A=255 */
        assert(q_image_pixels(img)[(8 * 16 + 8) * 4 + 0] == 200u);
        assert(q_image_pixels(img)[(8 * 16 + 8) * 4 + 1] ==  30u);
        assert(q_image_pixels(img)[(8 * 16 + 8) * 4 + 2] ==  30u);
        assert(q_image_pixels(img)[(8 * 16 + 8) * 4 + 3] == 255u);
        q_image_release(img);

        /* JPEG: load tests/images/icon_green.jpg (16×16, lossy — check dims only) */
        img = q_image_load_url("file://./tests/images/icon_green.jpg");
        assert(img != NULL);
        assert(q_image_width(img)  == 16);
        assert(q_image_height(img) == 16);
        assert(q_image_pixels(img) != NULL);
        /* JPEG is lossy; just verify the dominant channel is green */
        assert(q_image_pixels(img)[1] > q_image_pixels(img)[0]);
        assert(q_image_pixels(img)[1] > q_image_pixels(img)[2]);
        q_image_release(img);

        /* GIF: load tests/images/icon_blue.gif (16×16, lossless palette) */
        img = q_image_load_url("file://./tests/images/icon_blue.gif");
        assert(img != NULL);
        assert(q_image_width(img)  == 16);
        assert(q_image_height(img) == 16);
        assert(q_image_pixels(img) != NULL);
        /* corner (0,0): light gray background R=200 G=200 B=200 */
        assert(q_image_pixels(img)[0] == 200u);
        assert(q_image_pixels(img)[1] == 200u);
        assert(q_image_pixels(img)[2] == 200u);
        assert(q_image_pixels(img)[3] == 255u);
        /* center (8,8): blue — dominant channel is blue */
        assert(q_image_pixels(img)[(8 * 16 + 8) * 4 + 2] > q_image_pixels(img)[(8 * 16 + 8) * 4 + 0]);
        assert(q_image_pixels(img)[(8 * 16 + 8) * 4 + 2] > q_image_pixels(img)[(8 * 16 + 8) * 4 + 1]);
        q_image_release(img);
    }

    /* ── Inline image flow (img defaults to inline-level) ────────────────── */
    {
        q_document_t *img_doc;
        q_box_t *img_root;
        q_box_t *img_parent;
        q_box_t *img_ic;
        q_box_t *img_line;
        q_box_t *img_a;
        q_box_t *img_b;
        q_box_t *img_c;

        img_doc = q_document_create();
        assert(img_doc != NULL);
        assert(q_document_load_url(img_doc, "file://./tests/html/img_element.html") == 0);

        img_root = q_layout_build_tree(img_doc);
        assert(img_root != NULL);
        q_layout_measure(img_root, 240.0f, 0.0f);
        q_layout_position(img_root, 0.0f, 0.0f);

        img_parent = img_root->first_child;
        assert(img_parent != NULL);
        img_ic = img_parent->first_child;
        assert(img_ic != NULL);
        assert(img_ic->type == Q_BOX_INLINE_CONTAINER);
        img_line = img_ic->first_child;
        assert(img_line != NULL);
        assert(img_line->type == Q_BOX_LINE);

        img_a = img_line->first_child;
        assert(img_a != NULL);
        img_b = img_a->next_sibling;
        assert(img_b != NULL);
        img_c = img_b->next_sibling;
        assert(img_c != NULL);
        assert(img_c->next_sibling == NULL);

        assert(img_a->type == Q_BOX_IMAGE);
        assert(img_b->type == Q_BOX_IMAGE);
        assert(img_c->type == Q_BOX_IMAGE);
        assert(nearly_equal(img_a->y, img_b->y));
        assert(nearly_equal(img_b->y, img_c->y));
        assert(img_b->x > img_a->x);
        assert(img_c->x > img_b->x);

        q_layout_free_tree(img_root);
        q_document_destroy(img_doc);
    }

    /* ── overflow:hidden clipping ────────────────────────────────────────── */
    {
        q_document_t *ov_doc;
        q_box_t      *ov_root;
        q_box_t      *ov_parent;
        q_box_t      *ov_child;

        ov_doc = q_document_create();
        assert(ov_doc != NULL);
        assert(q_document_load_url(ov_doc, "file://./tests/html/overflow_hidden.html") == 0);

        ov_root = q_layout_build_tree(ov_doc);
        assert(ov_root != NULL);

        ov_parent = ov_root->first_child;
        assert(ov_parent != NULL);
        /* Verify overflow fields are parsed from CSS */
        assert(ov_parent->overflow_x == Q_OVERFLOW_HIDDEN);
        assert(ov_parent->overflow_y == Q_OVERFLOW_HIDDEN);

        q_layout_measure(ov_root, 400.0f, 0.0f);
        q_layout_position(ov_root, 0.0f, 0.0f);

        /* Parent is clamped to the explicit CSS dimensions */
        assert(nearly_equal(ov_parent->width,  100.0f));
        assert(nearly_equal(ov_parent->height,  40.0f));

        ov_child = ov_parent->first_child;
        assert(ov_child != NULL);
        assert(nearly_equal(ov_child->width,  200.0f));
        assert(nearly_equal(ov_child->height, 200.0f));
        assert(ov_parent->background_color == 0x4080C0FFu);
        assert(ov_child->background_color == 0xE04040FFu);

        q_paint_box(ov_root);

        /* Parent tile is exactly 100×40 */
        assert(ov_parent->tile != NULL);
        assert(ov_parent->tile_w == 100);
        assert(ov_parent->tile_h == 40);

        /* A pixel well inside the parent bounds (50,20) should be the child's
         * red color #e04040, since the child covers the whole content area. */
        assert_pixel_rgba(ov_parent->tile, ov_parent->tile_w, ov_parent->tile_h,
                          50, 20, 0xe0, 0x40, 0x40, 0xff);

        /* y=38 is the last row inside the clipped content area (the parent has
         * a 1-pixel default border, so the content region is y=1..38). */
        assert_pixel_rgba(ov_parent->tile, ov_parent->tile_w, ov_parent->tile_h,
                          50, 38, 0xe0, 0x40, 0x40, 0xff);

        q_layout_free_tree(ov_root);
        q_document_destroy(ov_doc);
    }

    /* ── Float layout context (phase 7) ──────────────────────────────────── */
    {
        q_document_t *flt_doc;
        q_box_t *flt_root;
        q_box_t *flt_container;
        q_box_t *flt_ic;
        q_box_t *flt_line;
        q_box_t *flt_img;
        q_box_t *flt_text;

        flt_doc = q_document_create();
        assert(flt_doc != NULL);
        assert(q_document_load_url(flt_doc, "file://./tests/html/float_text_wrap.html") == 0);

        flt_root = q_layout_build_tree(flt_doc);
        assert(flt_root != NULL);
        q_layout_measure(flt_root, 240.0f, 0.0f);
        q_layout_position(flt_root, 0.0f, 0.0f);

        flt_container = flt_root->first_child;
        assert(flt_container != NULL);
        flt_ic = flt_container->first_child;
        assert(flt_ic != NULL);
        flt_line = flt_ic->first_child;
        assert(flt_line != NULL);
        assert(flt_line->type == Q_BOX_LINE);

        flt_img = flt_line->first_child;
        assert(flt_img != NULL);
        assert(flt_img->type == Q_BOX_IMAGE);
        assert(flt_img->float_type == Q_FLOAT_LEFT);

        flt_text = flt_img->next_sibling;
        assert(flt_text != NULL);
        assert(flt_text->type == Q_BOX_TEXT);
        assert(flt_text->x >= flt_img->x + flt_img->width);

        q_layout_free_tree(flt_root);
        q_document_destroy(flt_doc);
    }

    /* ── clear property support (phase 8) ────────────────────────────────── */
    {
        q_document_t *clr_doc;
        q_box_t *clr_root;
        q_box_t *clr_container;
        q_box_t *clr_float;
        q_box_t *clr_clear;

        clr_doc = q_document_create();
        assert(clr_doc != NULL);
        assert(q_document_load_url(clr_doc, "file://./tests/html/clear_property.html") == 0);

        clr_root = q_layout_build_tree(clr_doc);
        assert(clr_root != NULL);
        q_layout_measure(clr_root, 240.0f, 0.0f);
        q_layout_position(clr_root, 0.0f, 0.0f);

        clr_container = clr_root->first_child;
        assert(clr_container != NULL);
        clr_float = clr_container->first_child;
        assert(clr_float != NULL);
        assert(clr_float->float_type == Q_FLOAT_LEFT);

        clr_clear = clr_float->next_sibling;
        assert(clr_clear != NULL);
        assert(clr_clear->clear_type == Q_CLEAR_LEFT);
        assert(clr_clear->y >= clr_float->y + clr_float->height);

        q_layout_free_tree(clr_root);
        q_document_destroy(clr_doc);
    }

    /* ── Table anonymous box fixup (phase 9) ─────────────────────────────── */
    {
        q_document_t *tbl_doc;
        q_box_t *tbl_root;
        q_box_t *table;
        q_box_t *table_child;
        q_box_t *sec1;
        q_box_t *sec2;
        q_box_t *row;
        q_box_t *cell;
        q_box_t *wrapped_block;

        tbl_doc = q_document_create();
        assert(tbl_doc != NULL);
        assert(q_document_load_url(tbl_doc, "file://./tests/html/table_anonymous_fixup.html") == 0);

        tbl_root = q_layout_build_tree(tbl_doc);
        assert(tbl_root != NULL);

        table = tbl_root->first_child;
        assert(table != NULL);
        assert(table->type == Q_BOX_TABLE);

        for (table_child = table->first_child; table_child != NULL; table_child = table_child->next_sibling) {
            assert(table_child->type != Q_BOX_TABLE_ROW);
            assert(table_child->type != Q_BOX_TABLE_CELL);
        }

        sec1 = table->first_child;
        assert(sec1 != NULL);
        assert(sec1->type == Q_BOX_TABLE_SECTION);
        row = sec1->first_child;
        assert(row != NULL);
        assert(row->type == Q_BOX_TABLE_ROW);
        cell = row->first_child;
        assert(cell != NULL);
        assert(cell->type == Q_BOX_TABLE_CELL);

        sec2 = sec1->next_sibling;
        assert(sec2 != NULL);
        assert(sec2->type == Q_BOX_TABLE_SECTION);
        wrapped_block = NULL;
        for (row = sec2->first_child; row != NULL && wrapped_block == NULL; row = row->next_sibling) {
            for (cell = row->first_child; cell != NULL; cell = cell->next_sibling) {
                if (cell->first_child != NULL && cell->first_child->type == Q_BOX_BLOCK) {
                    wrapped_block = cell->first_child;
                    break;
                }
            }
        }
        assert(wrapped_block != NULL);

        q_layout_free_tree(tbl_root);
        q_document_destroy(tbl_doc);
    }

    /* ── Root-level scrolling + Q_DIRTY_SCROLL ──────────────────────────── */
    {
        static const char scroll_html[] =
            "<html><body>"
            "<div style='height:20px;'>Top</div>"
            "<div style='height:80px;'>Bottom</div>"
            "</body></html>";
        q_document_t *scroll_doc;
        q_box_t *scroll_root;
        q_box_t *scroll_top;
        q_box_t *scroll_bottom;
        q_box_t *scroll_top_text;
        q_box_t *scroll_bottom_text;
        quanton_view_t scroll_view;
        q_event_t scroll_ev;

        scroll_doc = q_document_create();
        assert(scroll_doc != NULL);
        assert(q_document_load_html(scroll_doc, scroll_html, sizeof(scroll_html) - 1,
                                    "file://./tests/root_scroll.html") == 0);

        scroll_root = q_layout_build_tree(scroll_doc);
        assert(scroll_root != NULL);
        q_layout_measure(scroll_root, 120.0f, 0.0f);
        q_layout_position(scroll_root, 0.0f, 0.0f);
        assert(scroll_root->height >= 100.0f);

        scroll_top = scroll_root->first_child;
        assert(scroll_top != NULL);
        scroll_bottom = scroll_top->next_sibling;
        assert(scroll_bottom != NULL);
        assert(scroll_bottom->next_sibling == NULL);

        scroll_top_text = scroll_top->first_child->first_child->first_child;
        scroll_bottom_text = scroll_bottom->first_child->first_child->first_child;
        assert(scroll_top_text != NULL);
        assert(scroll_bottom_text != NULL);

        scroll_top->background_color = 0xC02020FFu;
        scroll_bottom->background_color = 0x2040C0FFu;

        q_paint_box(scroll_root);

        memset(&scroll_view, 0, sizeof(scroll_view));
        scroll_view.layout_root = scroll_root;
        scroll_view.vp_width = 120;
        scroll_view.vp_height = 40;
        scroll_view.on_event = capture_event_handler;

        q_composite_frame(&scroll_view);
        assert(nearly_equal(scroll_view.doc_width, 120.0f));
        assert(nearly_equal(scroll_view.doc_height, 100.0f));
        assert_pixel_rgba(scroll_view.framebuffer, scroll_view.vp_width, scroll_view.vp_height,
                          80, 10, 0xC0, 0x20, 0x20, 0xFF);

        q_view_scroll_to(&scroll_view, 0.0f, 60.0f);
        assert(nearly_equal(scroll_view.scroll_y, 60.0f));
        assert(scroll_view.dirty_flags == 0);
        assert_pixel_rgba(scroll_view.framebuffer, scroll_view.vp_width, scroll_view.vp_height,
                          80, 10, 0x20, 0x40, 0xC0, 0xFF);

        g_event_called = 0;
        g_event_target_box = NULL;
        g_event_target = NULL;
        memset(&scroll_ev, 0, sizeof(scroll_ev));
        scroll_ev.type = Q_EVENT_MOUSE_MOVE;
        scroll_ev.mouse_x = 10;
        scroll_ev.mouse_y = 10;
        q_event_dispatch(&scroll_view, &scroll_ev);
        assert(g_event_called == 1);
        assert(g_event_target_box == scroll_bottom_text);
        assert(g_event_target == scroll_bottom_text->dom_node);

        scroll_view.scroll_y = 0.0f;
        q_composite_frame(&scroll_view);

        g_event_called = 0;
        memset(&scroll_ev, 0, sizeof(scroll_ev));
        scroll_ev.type = Q_EVENT_MOUSE_WHEEL;
        scroll_ev.mouse_x = 10;
        scroll_ev.mouse_y = 10;
        scroll_ev.wheel_delta = -2;
        q_event_dispatch(&scroll_view, &scroll_ev);
        assert(g_event_called == 1);
        assert(nearly_equal(scroll_view.scroll_y, 60.0f));
        assert(scroll_view.dirty_flags == 0);
        assert_pixel_rgba(scroll_view.framebuffer, scroll_view.vp_width, scroll_view.vp_height,
                          80, 10, 0x20, 0x40, 0xC0, 0xFF);

        free(scroll_view.framebuffer);
        scroll_view.framebuffer = NULL;
        q_layout_free_tree(scroll_root);
        q_document_destroy(scroll_doc);
    }

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
        render_html_case_to_png("file://./tests/html/img_element.html", "output_img_element.png", TEST_WIDTH, TEST_HEIGHT);
        render_html_case_to_png("file://./tests/html/float_text_wrap.html", "output_float_text_wrap.png", TEST_WIDTH, TEST_HEIGHT);
        render_html_case_to_png("file://./tests/html/z_index_stack.html", "output_z_index_stack.png", TEST_WIDTH, TEST_HEIGHT);
        render_html_case_to_png("file://./tests/html/overflow_hidden.html", "output_overflow_hidden.png", TEST_WIDTH, TEST_HEIGHT);
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
