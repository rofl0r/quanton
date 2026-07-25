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
static const char *g_last_set_title;
static const char *g_navigate_href;

static q_box_t *find_box_for_dom_node(q_box_t *root, const lxb_dom_node_t *node)
{
    q_box_t *child;
    q_box_t *match;

    if (root == NULL) {
        return NULL;
    }
    if (root->dom_node == node) {
        return root;
    }

    for (child = root->first_child; child != NULL; child = child->next_sibling) {
        match = find_box_for_dom_node(child, node);
        if (match != NULL) {
            return match;
        }
    }

    return NULL;
}

static q_box_t *find_scroll_box(q_box_t *root)
{
    q_box_t *child;

    if (root == NULL) {
        return NULL;
    }

    if ((root->overflow_y == Q_OVERFLOW_AUTO || root->overflow_y == Q_OVERFLOW_SCROLL
         || root->overflow_x == Q_OVERFLOW_AUTO || root->overflow_x == Q_OVERFLOW_SCROLL)
        && root->first_child != NULL)
    {
        return root;
    }

    for (child = root->first_child; child != NULL; child = child->next_sibling) {
        q_box_t *match = find_scroll_box(child);
        if (match != NULL) {
            return match;
        }
    }

    return NULL;
}
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

static void assert_pixel_alpha_at_least(const uint8_t *pixels, int width, int height,
                                        int x, int y, uint8_t min_alpha)
{
    size_t idx;

    assert(pixels != NULL);
    assert(width > 0);
    assert(x >= 0);
    assert(y >= 0);
    assert(x < width);
    assert(y < height);

    idx = (size_t) (y * width + x) * 4u;
    assert(pixels[idx + 3] >= min_alpha);
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

static void capture_navigate_handler(quanton_view_t *view, const char *href, void *userdata)
{
    (void) view;
    (void) userdata;
    g_navigate_href = href;
}

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

static int mock_create_window(quanton_view_t *view, int w, int h, const char *title)
{
    (void) title;
    if (view != NULL) {
        view->vp_width = w;
        view->vp_height = h;
    }
    return 0;
}

static void mock_blit(quanton_view_t *view)
{
    (void) view;
}

static void mock_poll_events(quanton_view_t *view)
{
    (void) view;
}

static void mock_destroy_window(quanton_view_t *view)
{
    (void) view;
}

static void mock_set_title(quanton_view_t *view, const char *title)
{
    (void) view;
    g_last_set_title = title;
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
    /* body has 8px UA margin; origin was (10,20), so children start at (18,28)
     * and their width is reduced to 320 - 2*8 = 304. */
    assert(nearly_equal(root->margin_top, 8.0f));
    assert(nearly_equal(root->margin_right, 8.0f));
    assert(nearly_equal(root->margin_bottom, 8.0f));
    assert(nearly_equal(root->margin_left, 8.0f));
    assert(nearly_equal(first_block->x, 18.0f));
    assert(nearly_equal(first_block->y, 28.0f));
    assert(nearly_equal(first_block->width, 304.0f));
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
    /* first_text starts at (18,28) = origin(10,20) + body-margin(8,8) */
    assert(q_hit_test(root, 19, 29) == first_text);
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
        ev.mouse_x = 19;
        ev.mouse_y = 29;
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
        /* Image starts at body padding offset (8,8) in the root tile */
        assert_pixel_rgba(img_root->tile, img_root->tile_w, img_root->tile_h, 8, 8,
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
        assert(img_a->type == Q_BOX_IMAGE);

        /* The line may contain inter-image space word-boxes; skip them. */
        img_b = img_a->next_sibling;
        while (img_b != NULL && img_b->type == Q_BOX_TEXT) {
            img_b = img_b->next_sibling;
        }
        assert(img_b != NULL);
        assert(img_b->type == Q_BOX_IMAGE);

        img_c = img_b->next_sibling;
        while (img_c != NULL && img_c->type == Q_BOX_TEXT) {
            img_c = img_c->next_sibling;
        }
        assert(img_c != NULL);
        assert(img_c->type == Q_BOX_IMAGE);

        /* No further image siblings */
        {
            q_box_t *next = img_c->next_sibling;
            while (next != NULL && next->type == Q_BOX_TEXT) {
                next = next->next_sibling;
            }
            assert(next == NULL);
        }
        assert(nearly_equal(img_a->y, img_b->y));
        assert(nearly_equal(img_b->y, img_c->y));
        assert(img_b->x > img_a->x);
        assert(img_c->x > img_b->x);

        q_layout_free_tree(img_root);
        q_document_destroy(img_doc);
    }

    /* ── white-space: pre / nowrap, <br>, and inline-block ───────────────── */
    {
        static const char pre_html[] =
            "<html><body><div style='white-space:pre'>a  b\nc</div></body></html>";
        q_document_t *pre_doc;
        q_box_t *pre_root;
        q_box_t *pre_div;
        q_box_t *pre_ic;
        q_box_t *pre_line1;
        q_box_t *pre_line2;
        q_box_t *pre_text1;
        q_box_t *pre_text2;

        pre_doc = q_document_create();
        assert(pre_doc != NULL);
        assert(q_document_load_html(pre_doc, pre_html, sizeof(pre_html) - 1,
                                    "file://./tests/white_space_pre.html") == 0);

        pre_root = q_layout_build_tree(pre_doc);
        assert(pre_root != NULL);
        q_layout_measure(pre_root, 60.0f, 0.0f);
        q_layout_position(pre_root, 0.0f, 0.0f);

        pre_div = pre_root->first_child;
        assert(pre_div != NULL);
        pre_ic = pre_div->first_child;
        assert(pre_ic != NULL);
        assert(pre_ic->type == Q_BOX_INLINE_CONTAINER);
        assert(pre_ic->white_space == Q_WHITE_SPACE_PRE);

        pre_line1 = pre_ic->first_child;
        assert(pre_line1 != NULL);
        pre_line2 = pre_line1->next_sibling;
        assert(pre_line2 != NULL);
        assert(pre_line2->next_sibling == NULL);

        pre_text1 = pre_line1->first_child;
        pre_text2 = pre_line2->first_child;
        assert(pre_text1 != NULL && pre_text2 != NULL);
        assert(pre_text1->text_len == 4u);
        assert(strncmp(pre_text1->text, "a  b", 4u) == 0);
        assert(pre_text2->text_len == 1u);
        assert(strncmp(pre_text2->text, "c", 1u) == 0);

        q_layout_free_tree(pre_root);
        q_document_destroy(pre_doc);
    }

    {
        static const char br_html[] =
            "<html><body><div>one<br>two<br/>three</div></body></html>";
        q_document_t *br_doc;
        q_box_t *br_root;
        q_box_t *br_div;
        q_box_t *br_ic;
        q_box_t *br_line1;
        q_box_t *br_line2;
        q_box_t *br_line3;
        q_box_t *br_text1;
        q_box_t *br_text2;
        q_box_t *br_text3;

        br_doc = q_document_create();
        assert(br_doc != NULL);
        assert(q_document_load_html(br_doc, br_html, sizeof(br_html) - 1,
                                    "file://./tests/br.html") == 0);

        br_root = q_layout_build_tree(br_doc);
        assert(br_root != NULL);
        q_layout_measure(br_root, 240.0f, 0.0f);
        q_layout_position(br_root, 0.0f, 0.0f);

        br_div = br_root->first_child;
        assert(br_div != NULL);
        br_ic = br_div->first_child;
        assert(br_ic != NULL);
        assert(br_ic->type == Q_BOX_INLINE_CONTAINER);

        br_line1 = br_ic->first_child;
        assert(br_line1 != NULL);
        br_line2 = br_line1->next_sibling;
        assert(br_line2 != NULL);
        br_line3 = br_line2->next_sibling;
        assert(br_line3 != NULL);
        assert(br_line3->next_sibling == NULL);

        br_text1 = br_line1->first_child;
        br_text2 = br_line2->first_child;
        br_text3 = br_line3->first_child;
        assert(br_text1 != NULL && br_text2 != NULL && br_text3 != NULL);
        assert(br_text1->text_len == 3u && strncmp(br_text1->text, "one", 3u) == 0);
        assert(br_text2->text_len == 3u && strncmp(br_text2->text, "two", 3u) == 0);
        assert(br_text3->text_len == 5u && strncmp(br_text3->text, "three", 5u) == 0);

        q_layout_free_tree(br_root);
        q_document_destroy(br_doc);
    }

    {
        static const char nowrap_html[] =
            "<html><body><div style='white-space:nowrap'>one two three four five six</div></body></html>";
        q_document_t *nowrap_doc;
        q_box_t *nowrap_root;
        q_box_t *nowrap_div;
        q_box_t *nowrap_ic;
        q_box_t *nowrap_line;

        nowrap_doc = q_document_create();
        assert(nowrap_doc != NULL);
        assert(q_document_load_html(nowrap_doc, nowrap_html, sizeof(nowrap_html) - 1,
                                    "file://./tests/white_space_nowrap.html") == 0);

        nowrap_root = q_layout_build_tree(nowrap_doc);
        assert(nowrap_root != NULL);
        q_layout_measure(nowrap_root, 40.0f, 0.0f);
        q_layout_position(nowrap_root, 0.0f, 0.0f);

        nowrap_div = nowrap_root->first_child;
        assert(nowrap_div != NULL);
        nowrap_ic = nowrap_div->first_child;
        assert(nowrap_ic != NULL);
        assert(nowrap_ic->white_space == Q_WHITE_SPACE_NOWRAP);
        nowrap_line = nowrap_ic->first_child;
        assert(nowrap_line != NULL);
        assert(nowrap_line->next_sibling == NULL);

        q_layout_free_tree(nowrap_root);
        q_document_destroy(nowrap_doc);
    }

    {
        static const char ib_html[] =
            "<html><body><div>aa<span style='display:inline-block;width:30px;height:12px;'></span>bb</div></body></html>";
        q_document_t *ib_doc;
        q_box_t *ib_root;
        q_box_t *ib_div;
        q_box_t *ib_ic;
        q_box_t *ib_line;
        q_box_t *ib_a;
        q_box_t *ib_mid;
        q_box_t *ib_b;

        ib_doc = q_document_create();
        assert(ib_doc != NULL);
        assert(q_document_load_html(ib_doc, ib_html, sizeof(ib_html) - 1,
                                    "file://./tests/inline_block.html") == 0);

        ib_root = q_layout_build_tree(ib_doc);
        assert(ib_root != NULL);
        q_layout_measure(ib_root, 240.0f, 0.0f);
        q_layout_position(ib_root, 0.0f, 0.0f);

        ib_div = ib_root->first_child;
        assert(ib_div != NULL);
        ib_ic = ib_div->first_child;
        assert(ib_ic != NULL);
        assert(ib_ic->type == Q_BOX_INLINE_CONTAINER);

        ib_line = ib_ic->first_child;
        assert(ib_line != NULL);
        ib_a = ib_line->first_child;
        assert(ib_a != NULL);
        ib_mid = ib_a->next_sibling;
        assert(ib_mid != NULL);
        ib_b = ib_mid->next_sibling;
        assert(ib_b != NULL);
        assert(ib_b->next_sibling == NULL);

        assert(ib_mid->type == Q_BOX_BLOCK);
        assert(ib_mid->is_inline_block == 1);
        assert(nearly_equal(ib_mid->width, 30.0f));
        assert(nearly_equal(ib_mid->height, 12.0f));
        assert(ib_mid->x > ib_a->x);
        assert(ib_b->x > ib_mid->x);
        assert(nearly_equal(ib_a->y, ib_mid->y));
        assert(nearly_equal(ib_mid->y, ib_b->y));

        q_layout_free_tree(ib_root);
        q_document_destroy(ib_doc);
    }

    /* ── vertical-align on inline-level boxes ─────────────────────────────── */
    {
        static const char va_html[] =
            "<html><body><div>"
            "<span style='display:inline-block;width:8px;height:10px;'></span>"
            "<span style='display:inline-block;width:8px;height:20px;vertical-align:top;'></span>"
            "<span style='display:inline-block;width:8px;height:6px;vertical-align:bottom;'></span>"
            "<span style='display:inline-block;width:8px;height:10px;vertical-align:super;'></span>"
            "<span style='display:inline-block;width:8px;height:10px;vertical-align:sub;'></span>"
            "</div></body></html>";
        q_document_t *va_doc;
        q_box_t *va_root;
        q_box_t *va_div;
        q_box_t *va_ic;
        q_box_t *va_line;
        q_box_t *va_base;
        q_box_t *va_top;
        q_box_t *va_bottom;
        q_box_t *va_super;
        q_box_t *va_sub;

        va_doc = q_document_create();
        assert(va_doc != NULL);
        assert(q_document_load_html(va_doc, va_html, sizeof(va_html) - 1,
                                    "file://./tests/vertical_align.html") == 0);

        va_root = q_layout_build_tree(va_doc);
        assert(va_root != NULL);
        q_layout_measure(va_root, 320.0f, 0.0f);
        q_layout_position(va_root, 0.0f, 0.0f);

        va_div = va_root->first_child;
        assert(va_div != NULL);
        va_ic = va_div->first_child;
        assert(va_ic != NULL);
        va_line = va_ic->first_child;
        assert(va_line != NULL);

        va_base = va_line->first_child;
        assert(va_base != NULL);
        va_top = va_base->next_sibling;
        assert(va_top != NULL);
        va_bottom = va_top->next_sibling;
        assert(va_bottom != NULL);
        va_super = va_bottom->next_sibling;
        assert(va_super != NULL);
        va_sub = va_super->next_sibling;
        assert(va_sub != NULL);
        assert(va_sub->next_sibling == NULL);

        assert(va_top->vertical_align == Q_VERTICAL_ALIGN_TOP);
        assert(va_bottom->vertical_align == Q_VERTICAL_ALIGN_BOTTOM);
        assert(va_super->vertical_align == Q_VERTICAL_ALIGN_SUPER);
        assert(va_sub->vertical_align == Q_VERTICAL_ALIGN_SUB);

        assert(nearly_equal(va_top->y, va_base->y));
        assert(va_bottom->y > va_base->y);
        assert(va_super->y < va_base->y);
        assert(va_sub->y > va_base->y);

        q_layout_free_tree(va_root);
        q_document_destroy(va_doc);
    }

    /* ── text-decoration painting and inheritance to text runs ───────────── */
    {
        static const char td_html[] =
            "<html><body><div style='text-decoration:underline overline line-through;'>Decor</div></body></html>";
        q_document_t *td_doc;
        q_box_t *td_root;
        q_box_t *td_div;
        q_box_t *td_ic;
        q_box_t *td_line;
        q_box_t *td_text;
        int sample_x;
        int baseline_y;
        int underline_y;
        int strike_y;

        td_doc = q_document_create();
        assert(td_doc != NULL);
        assert(q_document_load_html(td_doc, td_html, sizeof(td_html) - 1,
                                    "file://./tests/text_decoration.html") == 0);

        td_root = q_layout_build_tree(td_doc);
        assert(td_root != NULL);
        q_layout_measure(td_root, 320.0f, 0.0f);
        q_layout_position(td_root, 0.0f, 0.0f);

        td_div = td_root->first_child;
        assert(td_div != NULL);
        td_ic = td_div->first_child;
        assert(td_ic != NULL);
        td_line = td_ic->first_child;
        assert(td_line != NULL);
        td_text = td_line->first_child;
        assert(td_text != NULL);
        assert((td_text->text_decoration & Q_TEXT_DECORATION_UNDERLINE) != 0u);
        assert((td_text->text_decoration & Q_TEXT_DECORATION_OVERLINE) != 0u);
        assert((td_text->text_decoration & Q_TEXT_DECORATION_LINE_THROUGH) != 0u);

        q_paint_box(td_root);
        assert(td_text->tile != NULL);
        sample_x = (td_text->tile_w > 2) ? 1 : 0;
        baseline_y = (td_text->run != NULL) ? (int) lroundf(td_text->run->ascender)
                                            : (int) lroundf(td_text->height * 0.8f);
        underline_y = baseline_y + 1;
        strike_y = (td_text->run != NULL)
                 ? (int) lroundf((float) baseline_y - (td_text->run->ascender * 0.5f))
                 : (int) lroundf(td_text->height * 0.5f);
        if (underline_y < 0) {
            underline_y = 0;
        }
        if (underline_y >= td_text->tile_h) {
            underline_y = td_text->tile_h - 1;
        }
        if (strike_y < 0) {
            strike_y = 0;
        }
        if (strike_y >= td_text->tile_h) {
            strike_y = td_text->tile_h - 1;
        }
        assert_pixel_alpha_at_least(td_text->tile, td_text->tile_w, td_text->tile_h,
                                    sample_x, 0, 1);
        assert_pixel_alpha_at_least(td_text->tile, td_text->tile_w, td_text->tile_h,
                                    sample_x, underline_y, 1);
        assert_pixel_alpha_at_least(td_text->tile, td_text->tile_w, td_text->tile_h,
                                    sample_x, strike_y, 1);

        q_layout_free_tree(td_root);
        q_document_destroy(td_doc);
    }

    /* ── border-radius clipping and background-image tiling ───────────────── */
    {
        static const char brad_html[] =
            "<html><body><div style='width:20px;height:20px;background:#ff0000;border-radius:8px;'></div></body></html>";
        q_document_t *brad_doc;
        q_box_t *brad_root;
        q_box_t *brad_div;

        brad_doc = q_document_create();
        assert(brad_doc != NULL);
        assert(q_document_load_html(brad_doc, brad_html, sizeof(brad_html) - 1,
                                    "file://./tests/border_radius.html") == 0);

        brad_root = q_layout_build_tree(brad_doc);
        assert(brad_root != NULL);
        q_layout_measure(brad_root, 320.0f, 0.0f);
        q_layout_position(brad_root, 0.0f, 0.0f);
        brad_div = brad_root->first_child;
        assert(brad_div != NULL);
        brad_div->border_width[0] = 0.0f;
        brad_div->border_width[1] = 0.0f;
        brad_div->border_width[2] = 0.0f;
        brad_div->border_width[3] = 0.0f;

        q_paint_box(brad_root);
        assert(brad_div->tile != NULL);
        assert_pixel_rgba(brad_div->tile, brad_div->tile_w, brad_div->tile_h, 0, 0, 0, 0, 0, 0);
        assert_pixel_rgba(brad_div->tile, brad_div->tile_w, brad_div->tile_h, 10, 10, 255, 0, 0, 255);

        q_layout_free_tree(brad_root);
        q_document_destroy(brad_doc);
    }

    {
        static const char bg_html[] =
            "<html><body><div style='width:32px;height:24px;background-image:url(../images/icon_red.png);background-repeat:repeat;'></div></body></html>";
        q_document_t *bg_doc;
        q_box_t *bg_root;
        q_box_t *bg_div;

        bg_doc = q_document_create();
        assert(bg_doc != NULL);
        assert(q_document_load_html(bg_doc, bg_html, sizeof(bg_html) - 1,
                                    "file://./tests/html/background_image.html") == 0);

        bg_root = q_layout_build_tree(bg_doc);
        assert(bg_root != NULL);
        q_layout_measure(bg_root, 320.0f, 0.0f);
        q_layout_position(bg_root, 0.0f, 0.0f);
        bg_div = bg_root->first_child;
        assert(bg_div != NULL);
        assert(bg_div->background_image != NULL);
        assert(bg_div->background_repeat == Q_BACKGROUND_REPEAT_REPEAT);
        bg_div->border_width[0] = 0.0f;
        bg_div->border_width[1] = 0.0f;
        bg_div->border_width[2] = 0.0f;
        bg_div->border_width[3] = 0.0f;

        q_paint_box(bg_root);
        assert(bg_div->tile != NULL);
        assert_pixel_rgba(bg_div->tile, bg_div->tile_w, bg_div->tile_h, 0, 0, 220, 50, 50, 255);
        assert_pixel_rgba(bg_div->tile, bg_div->tile_w, bg_div->tile_h, 16, 0, 220, 50, 50, 255);
        assert_pixel_rgba(bg_div->tile, bg_div->tile_w, bg_div->tile_h, 0, 16, 220, 50, 50, 255);

        q_layout_free_tree(bg_root);
        q_document_destroy(bg_doc);
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
         * no default border, so the content region is y=0..39). */
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

    /* ── Margin support in block flow ───────────────────────────────────── */
    {
        static const char margin_html[] =
            "<html><body style='margin:0'>"
            "<div style='height:10px; margin-bottom:12px;'>A</div>"
            "<div style='height:10px; margin-top:4px;'>B</div>"
            "</body></html>";
        q_document_t *margin_doc;
        q_box_t *margin_root;
        q_box_t *margin_a;
        q_box_t *margin_b;

        margin_doc = q_document_create();
        assert(margin_doc != NULL);
        assert(q_document_load_html(margin_doc, margin_html, sizeof(margin_html) - 1,
                                    "file://./tests/margins.html") == 0);

        margin_root = q_layout_build_tree(margin_doc);
        assert(margin_root != NULL);
        q_layout_measure(margin_root, 200.0f, 0.0f);
        q_layout_position(margin_root, 0.0f, 0.0f);

        margin_a = margin_root->first_child;
        assert(margin_a != NULL);
        margin_b = margin_a->next_sibling;
        assert(margin_b != NULL);

        assert(nearly_equal(margin_a->margin_bottom, 12.0f));
        assert(nearly_equal(margin_b->margin_top, 4.0f));
        assert(nearly_equal(margin_b->y, margin_a->y + margin_a->height + 12.0f + 4.0f));

        q_layout_free_tree(margin_root);
        q_document_destroy(margin_doc);
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
        assert(nearly_equal(scroll_view.doc_height, 116.0f)); /* 8px body margin top+bottom + 20+80 */
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
        assert(nearly_equal(scroll_view.scroll_y, 76.0f)); /* wheel 2*40=80, clamped to doc_h(116)-vp_h(40) */
        assert(scroll_view.dirty_flags == 0);
        assert_pixel_rgba(scroll_view.framebuffer, scroll_view.vp_width, scroll_view.vp_height,
                          80, 10, 0x20, 0x40, 0xC0, 0xFF);

        free(scroll_view.framebuffer);
        scroll_view.framebuffer = NULL;
        q_layout_free_tree(scroll_root);
        q_document_destroy(scroll_doc);
    }

    /* ── Inner scroll container repaint path (Q_DIRTY_RECOMPOSE) ────────── */
    {
        static const char inner_scroll_html[] =
            "<html><body>"
            "<div style='width:120px;height:60px;overflow:auto;'>"
            "<div style='height:60px;'>First</div>"
            "<div style='height:60px;'>Second</div>"
            "</div>"
            "</body></html>";
        q_document_t *inner_doc;
        q_box_t *inner_root;
        q_box_t *inner_scroll;
        q_box_t *inner_first;
        q_box_t *inner_second;
        quanton_view_t inner_view;

        inner_doc = q_document_create();
        assert(inner_doc != NULL);
        assert(q_document_load_html(inner_doc, inner_scroll_html, sizeof(inner_scroll_html) - 1,
                                    "file://./tests/inner_scroll.html") == 0);

        inner_root = q_layout_build_tree(inner_doc);
        assert(inner_root != NULL);
        q_layout_measure(inner_root, 160.0f, 0.0f);
        q_layout_position(inner_root, 0.0f, 0.0f);

        inner_scroll = find_scroll_box(inner_root);
        assert(inner_scroll != NULL);
        inner_first = inner_scroll->first_child;
        inner_second = inner_first->next_sibling;
        assert(inner_first != NULL);
        assert(inner_second != NULL);

        inner_first->background_color = 0xFF0000FFu;
        inner_second->background_color = 0x0000FFFFu;

        memset(&inner_view, 0, sizeof(inner_view));
        inner_view.layout_root = inner_root;
        inner_view.vp_width = 120;
        inner_view.vp_height = 60;

        q_paint_box(inner_root);
        q_composite_frame(&inner_view);
        assert_pixel_rgba(inner_view.framebuffer, inner_view.vp_width, inner_view.vp_height,
                          10, 10, 0xFF, 0x00, 0x00, 0xFF);

        inner_scroll->scroll_y = 60.0f;
        inner_view.dirty_flags = Q_DIRTY_RECOMPOSE;
        q_view_update(&inner_view);
        assert_pixel_rgba(inner_view.framebuffer, inner_view.vp_width, inner_view.vp_height,
                          10, 10, 0x00, 0x00, 0xFF, 0xFF);

        free(inner_view.framebuffer);
        inner_view.framebuffer = NULL;
        q_layout_free_tree(inner_root);
        q_document_destroy(inner_doc);
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

        assert(nearly_equal(flex_a->width, flex_container->width / 3.0f));
        assert(nearly_equal(flex_b->width, flex_container->width / 3.0f));
        assert(nearly_equal(flex_c->width, flex_container->width / 3.0f));
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

    /* ── List items + markers (phase 19) ───────────────────────────────── */
    {
        q_document_t *list_doc;
        q_box_t *list_root;
        q_box_t *list_ul;
        q_box_t *list_ol;
        q_box_t *list_ul_li;
        q_box_t *list_ol_li1;
        q_box_t *list_ol_li2;
        int marker_x;
        int marker_y;

        list_doc = q_document_create();
        assert(list_doc != NULL);
        assert(q_document_load_url(list_doc, "file://./tests/html/list_ol_ul.html") == 0);

        list_root = q_layout_build_tree(list_doc);
        assert(list_root != NULL);
        q_layout_measure(list_root, 320.0f, 0.0f);
        q_layout_position(list_root, 0.0f, 0.0f);
        q_paint_box(list_root);

        list_ul = list_root->first_child;
        assert(list_ul != NULL);
        list_ol = list_ul->next_sibling;
        assert(list_ol != NULL);
        assert(nearly_equal(list_ul->padding_left, 40.0f));
        assert(nearly_equal(list_ol->padding_left, 40.0f));

        list_ul_li = list_ul->first_child;
        assert(list_ul_li != NULL);
        assert(list_ul_li->list_style_type == Q_LIST_STYLE_DISC);
        assert(list_ul_li->list_item_index == 1);

        list_ol_li1 = list_ol->first_child;
        assert(list_ol_li1 != NULL);
        list_ol_li2 = list_ol_li1->next_sibling;
        assert(list_ol_li2 != NULL);
        assert(list_ol_li1->list_style_type == Q_LIST_STYLE_DECIMAL);
        assert(list_ol_li1->list_item_index == 1);
        assert(list_ol_li2->list_item_index == 2);

        marker_x = (int) lroundf(list_ul_li->x + 4.0f);
        marker_y = (int) lroundf(list_ul_li->y + (list_ul_li->height * 0.5f));
        assert_pixel_rgba(list_root->tile, list_root->tile_w, list_root->tile_h,
                          marker_x, marker_y, 0, 0, 0, 255);

        q_layout_free_tree(list_root);
        q_document_destroy(list_doc);
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
        render_html_case_to_png("file://./tests/html/table_border_collapse.html", "output_table_border_collapse.png", TEST_WIDTH, TEST_HEIGHT);
        render_html_case_to_png("file://./tests/html/table_header_cells.html", "output_table_header_cells.png", TEST_WIDTH, TEST_HEIGHT);
        render_html_case_to_png("file://./tests/html/list_ol_ul.html", "output_list_ol_ul.png", TEST_WIDTH, TEST_HEIGHT);
        render_html_case_to_png("file://./tests/html/bg_image.html", "output_bg_image.png", TEST_WIDTH, TEST_HEIGHT);
        render_html_case_to_png("file://./tests/html/percent_width_table.html", "output_percent_width_table.png", TEST_WIDTH, TEST_HEIGHT);
        render_html_case_to_png("file://./tests/html/headings.html", "output_headings.png", TEST_WIDTH, TEST_HEIGHT);
        render_html_case_to_png("file://./tests/html/bold_italic.html", "output_bold_italic.png", TEST_WIDTH, TEST_HEIGHT);
        render_html_case_to_png("file://./tests/html/hr.html", "output_hr.png", TEST_WIDTH, TEST_HEIGHT);
        render_html_case_to_png("file://./tests/html/code_kbd_tt.html", "output_code_kbd_tt.png", TEST_WIDTH, TEST_HEIGHT);
        render_html_case_to_png("file://./tests/html/blockquote.html", "output_blockquote.png", TEST_WIDTH, TEST_HEIGHT);
        render_html_case_to_png("file://./tests/html/strikethrough.html", "output_strikethrough.png", TEST_WIDTH, TEST_HEIGHT);
        render_html_case_to_png("file://./tests/html/sup_sub.html", "output_sup_sub.png", TEST_WIDTH, TEST_HEIGHT);
        render_html_case_to_png("file://./tests/html/anchor_link.html", "output_anchor_link.png", TEST_WIDTH, TEST_HEIGHT);
        render_html_case_to_png("file://./tests/html/anchor_scroll.html", "output_anchor_scroll.png", TEST_WIDTH, TEST_HEIGHT);
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

    /* ── Table layout: measure and position ──────────────────────────────── */

    /* Test 1: simple 2x2 table fills full width, two equal columns */
    {
        static const char tbl_html[] =
            "<html><body><table id='t' style='width:200px;'>"
            "<tr><td>A</td><td>B</td></tr>"
            "<tr><td>C</td><td>D</td></tr>"
            "</table></body></html>";

        q_document_t *tdoc  = q_document_create();
        q_box_t      *troot = NULL;
        q_box_t      *table, *section, *row1, *row2;
        q_box_t      *cell00, *cell01, *cell10, *cell11;

        assert(tdoc != NULL);
        assert(q_document_load_html(tdoc, tbl_html,
                                    sizeof(tbl_html) - 1, NULL) == 0);
        troot = q_layout_build_tree(tdoc);
        assert(troot != NULL);

        q_layout_measure(troot, 800.0f, 600.0f);
        q_layout_position(troot, 0.0f, 0.0f);

        table   = troot->first_child;
        assert(table != NULL);
        assert(table->type == Q_BOX_TABLE);
        assert(table->table != NULL);
        assert(table->table->col_count == 2);
        assert(table->table->row_count == 2);
        /* table was given explicit style width 200 px */
        assert(fabsf(table->width - 200.0f) < 1.0f);

        section = table->first_child;
        assert(section != NULL);
        assert(section->type == Q_BOX_TABLE_SECTION);

        row1    = section->first_child;
        assert(row1 != NULL);
        assert(row1->type == Q_BOX_TABLE_ROW);
        /* Row x equals table x (rows span full table width) */
        assert(fabsf(row1->x - table->x) < 1.0f);

        row2    = row1->next_sibling;
        assert(row2 != NULL);

        cell00  = row1->first_child;
        cell01  = cell00->next_sibling;
        assert(cell00 != NULL && cell01 != NULL);
        assert(cell00->type == Q_BOX_TABLE_CELL);

        /* Both columns share the available width (200 minus border-spacing gaps) */
        assert(cell00->width > 0.0f);
        assert(cell01->width > 0.0f);
        assert(fabsf(cell00->width + cell01->width
                     - (200.0f - (float)(table->table->col_count + 1)
                                 * table->table->border_spacing)) < 2.0f);

        /* cell01 starts after cell00 */
        assert(cell01->x > cell00->x);

        /* Row 2 cells start below row 1 */
        cell10  = row2->first_child;
        cell11  = (cell10 != NULL) ? cell10->next_sibling : NULL;
        assert(cell10 != NULL && cell11 != NULL);
        assert(cell10->y > cell00->y);
        assert(fabsf(cell10->y - row2->y) < 1.0f);

        q_layout_free_tree(troot);
        q_document_destroy(tdoc);
    }

    /* Test 2: colspan spanning 2 of 3 columns */
    {
        static const char cs_html[] =
            "<html><body><table style='width:300px;'>"
            "<tr><td colspan='2'>Wide</td><td>Right</td></tr>"
            "<tr><td>A</td><td>B</td><td>C</td></tr>"
            "</table></body></html>";

        q_document_t *csdoc  = q_document_create();
        q_box_t      *csroot = NULL;
        q_box_t      *table, *section, *row1, *row2;
        q_box_t      *wide_cell, *right_cell, *a_cell, *b_cell, *c_cell;

        assert(csdoc != NULL);
        assert(q_document_load_html(csdoc, cs_html,
                                    sizeof(cs_html) - 1, NULL) == 0);
        csroot = q_layout_build_tree(csdoc);
        assert(csroot != NULL);

        q_layout_measure(csroot, 800.0f, 600.0f);
        q_layout_position(csroot, 0.0f, 0.0f);

        table   = csroot->first_child;
        assert(table != NULL && table->type == Q_BOX_TABLE);
        assert(table->table != NULL);
        assert(table->table->col_count == 3);
        assert(table->table->row_count == 2);

        section    = table->first_child;
        row1       = section->first_child;
        row2       = row1->next_sibling;
        wide_cell  = row1->first_child;
        right_cell = (wide_cell != NULL) ? wide_cell->next_sibling : NULL;
        a_cell     = (row2 != NULL)      ? row2->first_child       : NULL;
        b_cell     = (a_cell != NULL)    ? a_cell->next_sibling    : NULL;
        c_cell     = (b_cell != NULL)    ? b_cell->next_sibling    : NULL;

        assert(wide_cell != NULL && right_cell != NULL);
        assert(a_cell != NULL && b_cell != NULL && c_cell != NULL);

        /* Combined width: wide_cell (colspan=2) includes the one inter-column
         * border_spacing between the two spanned columns, so together with
         * right_cell they total table_width minus col_count spacings
         * (the two outer spacings are excluded). */
        assert(wide_cell->width > 0.0f);
        assert(right_cell->width > 0.0f);
        assert(fabsf(wide_cell->width + right_cell->width
                     - (300.0f - (float)(table->table->col_count)
                                 * table->table->border_spacing)) < 2.0f);
        /* wide_cell starts at first col offset: table->x + border_spacing */
        assert(fabsf(wide_cell->x - (table->x + table->table->border_spacing)) < 1.0f);
        assert(right_cell->x > wide_cell->x);

        /* Second row: three cells side by side */
        assert(a_cell->x < b_cell->x);
        assert(b_cell->x < c_cell->x);
        /* Row 2 starts below row 1 */
        assert(a_cell->y > wide_cell->y);

        q_layout_free_tree(csroot);
        q_document_destroy(csdoc);
    }

    /* Test 3: rowspan spanning 2 rows */
    {
        static const char rs_html[] =
            "<html><body><table style='width:200px;'>"
            "<tr><td rowspan='2'>Tall</td><td>Top right</td></tr>"
            "<tr><td>Bottom right</td></tr>"
            "</table></body></html>";

        q_document_t *rsdoc  = q_document_create();
        q_box_t      *rsroot = NULL;
        q_box_t      *table, *section, *row1, *row2;
        q_box_t      *tall_cell, *topright_cell, *botright_cell;

        assert(rsdoc != NULL);
        assert(q_document_load_html(rsdoc, rs_html,
                                    sizeof(rs_html) - 1, NULL) == 0);
        rsroot = q_layout_build_tree(rsdoc);
        assert(rsroot != NULL);

        q_layout_measure(rsroot, 800.0f, 600.0f);
        q_layout_position(rsroot, 0.0f, 0.0f);

        table       = rsroot->first_child;
        assert(table != NULL && table->type == Q_BOX_TABLE);
        assert(table->table != NULL);
        assert(table->table->col_count == 2);
        assert(table->table->row_count == 2);

        section       = table->first_child;
        row1          = section->first_child;
        row2          = row1->next_sibling;
        tall_cell     = row1->first_child;
        topright_cell = (tall_cell != NULL) ? tall_cell->next_sibling : NULL;
        botright_cell = (row2 != NULL)      ? row2->first_child       : NULL;

        assert(tall_cell != NULL && topright_cell != NULL && botright_cell != NULL);

        /* Tall cell's height >= sum of the two rows */
        assert(tall_cell->height >= table->table->rows[0].height
                                   + table->table->rows[1].height - 1.0f);
        /* Tall cell starts at first col offset: table->x + border_spacing */
        assert(fabsf(tall_cell->x - (table->x + table->table->border_spacing)) < 1.0f);
        assert(topright_cell->x > tall_cell->x);
        /* Bottom-right cell is below top-right */
        assert(botright_cell->y > topright_cell->y);

        q_layout_free_tree(rsroot);
        q_document_destroy(rsdoc);
    }

    /* Test 4: border-collapse keeps single interior borders */
    {
        q_document_t *bdoc = q_document_create();
        q_box_t      *broot;
        q_box_t      *table;
        int           boundary_x;
        int           sample_y;

        assert(bdoc != NULL);
        assert(q_document_load_url(bdoc, "file://./tests/html/table_border_collapse.html") == 0);
        broot = q_layout_build_tree(bdoc);
        assert(broot != NULL);

        q_layout_measure(broot, 800.0f, 600.0f);
        q_layout_position(broot, 0.0f, 0.0f);

        table = broot->first_child;
        assert(table != NULL);
        assert(table->type == Q_BOX_TABLE);
        assert(table->table != NULL);
        assert(table->table->border_collapse == 1);

        q_paint_box(broot);
        assert(broot->tile != NULL);

        boundary_x = (int) lroundf(table->x + table->table->cols[0].final_width);
        sample_y = (int) lroundf(table->y + table->table->rows[0].height + 8.0f);

        assert_pixel_rgba(broot->tile, broot->tile_w, broot->tile_h,
                          boundary_x, sample_y, 48, 48, 48, 255);
        assert(broot->tile[((size_t) sample_y * (size_t) broot->tile_w + (size_t) (boundary_x + 1)) * 4u + 0] != 48
               || broot->tile[((size_t) sample_y * (size_t) broot->tile_w + (size_t) (boundary_x + 1)) * 4u + 1] != 48
               || broot->tile[((size_t) sample_y * (size_t) broot->tile_w + (size_t) (boundary_x + 1)) * 4u + 2] != 48);

        q_layout_free_tree(broot);
        q_document_destroy(bdoc);
    }

    /* Test 5: TH background paint differs from body cells */
    {
        q_document_t *hdoc = q_document_create();
        q_box_t      *hroot;
        q_box_t      *table;
        q_box_t      *thead_sec;
        q_box_t      *tbody_sec;
        q_box_t      *thead_row;
        q_box_t      *tbody_row;
        int           sample_x;
        int           header_y;
        int           body_y;

        assert(hdoc != NULL);
        assert(q_document_load_url(hdoc, "file://./tests/html/table_header_cells.html") == 0);
        hroot = q_layout_build_tree(hdoc);
        assert(hroot != NULL);

        q_layout_measure(hroot, 800.0f, 600.0f);
        q_layout_position(hroot, 0.0f, 0.0f);
        q_paint_box(hroot);
        assert(hroot->tile != NULL);

        table = hroot->first_child;
        assert(table != NULL && table->type == Q_BOX_TABLE);
        thead_sec = table->first_child;
        assert(thead_sec != NULL);
        tbody_sec = thead_sec->next_sibling;
        assert(tbody_sec != NULL);
        thead_row = thead_sec->first_child;
        tbody_row = tbody_sec->first_child;
        assert(thead_row != NULL && tbody_row != NULL);

        sample_x = (int) lroundf(table->x + 100.0f);
        header_y = (int) lroundf(thead_row->y + 8.0f);
        body_y   = (int) lroundf(tbody_row->y + 8.0f);

        assert_pixel_rgba(hroot->tile, hroot->tile_w, hroot->tile_h,
                          sample_x, header_y, 216, 226, 242, 255);
        assert_pixel_rgba(hroot->tile, hroot->tile_w, hroot->tile_h,
                          sample_x, body_y, 247, 247, 247, 255);

        q_layout_free_tree(hroot);
        q_document_destroy(hdoc);
    }

    /* Test 6: getElementById and setInnerHTML DOM helpers */
    {
        static const char id_html[] =
            "<html><body>"
            "<div id='target'>original</div>"
            "</body></html>";

        q_document_t    *id_doc  = q_document_create();
        quanton_view_t   id_view;
        lxb_dom_element_t *target_el;

        assert(id_doc != NULL);
        assert(q_document_load_html(id_doc, id_html,
                                    sizeof(id_html) - 1, NULL) == 0);

        memset(&id_view, 0, sizeof(id_view));
        id_view.document   = id_doc;
        id_view.vp_width   = 800;
        id_view.vp_height  = 600;
        id_view.layout_root = q_layout_build_tree(id_doc);
        assert(id_view.layout_root != NULL);
        q_layout_measure(id_view.layout_root, 800.0f, 600.0f);
        q_layout_position(id_view.layout_root, 0.0f, 0.0f);

        /* getElementById */
        target_el = q_dom_get_element_by_id(&id_view, "target");
        assert(target_el != NULL);

        /* setInnerHTML replaces children and marks dirty */
        {
            static const char new_html[] = "<span>replaced</span>";
            assert(q_dom_set_inner_html(&id_view, target_el,
                                        new_html,
                                        sizeof(new_html) - 1) == 0);
            assert((id_view.dirty_flags & Q_DIRTY_LAYOUT) != 0);
        }

        /* After q_view_update, layout must be rebuilt successfully */
        q_view_update(&id_view);
        assert(id_view.dirty_flags == 0);
        assert(id_view.layout_root != NULL);

        q_layout_free_tree(id_view.layout_root);
        q_document_destroy(id_doc);
    }

    /* ── UA defaults: h1-h6, b/strong, i/em, hr ────────────────────────── */

    /* h1-h6: font-size and font-weight defaults */
    {
        static const char hdr_html[] =
            "<html><body>"
            "<h1>A</h1><h2>B</h2><h3>C</h3>"
            "<h4>D</h4><h5>E</h5><h6>F</h6>"
            "</body></html>";
        static const float expected_px[6] = {32.0f, 24.0f, 18.72f, 16.0f, 13.28f, 10.72f};

        q_document_t *hdoc  = q_document_create();
        q_box_t      *hroot = NULL;
        q_box_t      *h;
        int           level;

        assert(hdoc != NULL);
        assert(q_document_load_html(hdoc, hdr_html,
                                    sizeof(hdr_html) - 1, NULL) == 0);
        hroot = q_layout_build_tree(hdoc);
        assert(hroot != NULL);
        q_layout_measure(hroot, 800.0f, 600.0f);
        q_layout_position(hroot, 0.0f, 0.0f);

        /* q_layout_build_tree returns the body box directly; its first_child
         * is h1 (no extra body wrapper). */
        h = hroot->first_child;
        for (level = 0; level < 6; ++level) {
            assert(h != NULL);
            assert(h->type == Q_BOX_BLOCK);
            assert(fabsf(h->font_size - expected_px[level]) < 0.1f);
            assert(h->font_weight == 700);
            assert(h->margin_top    > 0.0f);
            assert(h->margin_bottom > 0.0f);
            h = h->next_sibling;
        }

        q_layout_free_tree(hroot);
        q_document_destroy(hdoc);
    }

    /* b/strong → font_weight 700; i/em → font_style italic */
    {
        static const char bi_html[] =
            "<html><body>"
            "<b>bold</b><strong>strong</strong>"
            "<i>ital</i><em>em</em>"
            "</body></html>";
        q_document_t *bdoc  = q_document_create();
        q_box_t      *broot = NULL;
        q_box_t      *body2;

        assert(bdoc != NULL);
        assert(q_document_load_html(bdoc, bi_html,
                                    sizeof(bi_html) - 1, NULL) == 0);
        broot = q_layout_build_tree(bdoc);
        assert(broot != NULL);
        /* Check font properties right after tree build, before measure() moves
         * inline-block boxes into LINE wrappers. */

        body2 = broot;  /* q_layout_build_tree returns the body box directly */
        assert(body2 != NULL);

        /* Check font_weight on b/strong boxes directly; they are inline-block
         * Q_BOX_BLOCK children of the anonymous inline container. */
        {
            q_box_t *b_box = NULL, *strong_box = NULL;
            q_box_t *i_box = NULL, *em_box = NULL;
            q_box_t *ic, *kid;
            /* body's first child is an anonymous inline container */
            ic = body2->first_child;
            assert(ic != NULL && ic->type == Q_BOX_INLINE_CONTAINER);
            for (kid = ic->first_child; kid != NULL; kid = kid->next_sibling) {
                if (kid->is_inline_block) {
                    if (kid->font_weight == 700 && b_box == NULL)
                        b_box = kid;
                    else if (kid->font_weight == 700)
                        strong_box = kid;
                    else if (kid->font_style == Q_FONT_STYLE_ITALIC && i_box == NULL)
                        i_box = kid;
                    else if (kid->font_style == Q_FONT_STYLE_ITALIC)
                        em_box = kid;
                }
            }
            assert(b_box != NULL);
            assert(strong_box != NULL);
            assert(i_box != NULL);
            assert(em_box != NULL);
        }

        q_layout_free_tree(broot);
        q_document_destroy(bdoc);
    }

    /* hr: style_height=0, top border=1px, margin-block=4px */
    {
        static const char hr_html[] =
            "<html><body><p>Before</p><hr><p>After</p></body></html>";
        q_document_t *hdoc2 = q_document_create();
        q_box_t      *hroot2 = NULL;
        q_box_t      *p1, *hr_box, *p2;

        assert(hdoc2 != NULL);
        assert(q_document_load_html(hdoc2, hr_html,
                                    sizeof(hr_html) - 1, NULL) == 0);
        hroot2 = q_layout_build_tree(hdoc2);
        assert(hroot2 != NULL);
        q_layout_measure(hroot2, 800.0f, 600.0f);
        q_layout_position(hroot2, 0.0f, 0.0f);

        /* q_layout_build_tree returns the body box directly; its children
         * are p1, hr, p2 in order. */
        p1     = hroot2->first_child;
        assert(p1 != NULL);
        hr_box = p1->next_sibling;
        assert(hr_box != NULL);
        p2     = hr_box->next_sibling;
        assert(p2 != NULL);

        assert(hr_box->type == Q_BOX_BLOCK);
        assert(fabsf(hr_box->style_height) < 0.01f);        /* height: 0 */
        assert(fabsf(hr_box->border_width[0] - 1.0f) < 0.01f); /* 1px top border */
        assert(fabsf(hr_box->border_color[0] - (float) 0x888888FFu) < 1.0f);
        assert(hr_box->margin_top    >= 4.0f);
        assert(hr_box->margin_bottom >= 4.0f);
        /* hr is rendered between the two paragraphs */
        assert(hr_box->y > p1->y);
        assert(p2->y     > hr_box->y);

        q_layout_free_tree(hroot2);
        q_document_destroy(hdoc2);
    }

    /* code/kbd/tt: monospace family defaults */
    {
        static const char code_html[] =
            "<html><body><p><code>a</code><kbd>b</kbd><tt>c</tt></p></body></html>";
        q_document_t *cdoc = q_document_create();
        q_box_t *croot = NULL;
        q_box_t *p, *ic, *kid;
        int mono_count = 0;

        assert(cdoc != NULL);
        assert(q_document_load_html(cdoc, code_html, sizeof(code_html) - 1, NULL) == 0);
        croot = q_layout_build_tree(cdoc);
        assert(croot != NULL);
        p = croot->first_child;
        assert(p != NULL);
        ic = p->first_child;
        assert(ic != NULL && ic->type == Q_BOX_INLINE_CONTAINER);
        for (kid = ic->first_child; kid != NULL; kid = kid->next_sibling) {
            if (kid->is_inline_block && kid->font_family != NULL
                && strcmp(kid->font_family, "monospace") == 0) {
                ++mono_count;
            }
        }
        assert(mono_count == 3);

        q_layout_free_tree(croot);
        q_document_destroy(cdoc);
    }

    /* blockquote: UA default horizontal margins */
    {
        static const char bq_html[] =
            "<html><body><blockquote>q</blockquote></body></html>";
        q_document_t *bqdoc = q_document_create();
        q_box_t *bqroot = NULL;
        q_box_t *bq;

        assert(bqdoc != NULL);
        assert(q_document_load_html(bqdoc, bq_html, sizeof(bq_html) - 1, NULL) == 0);
        bqroot = q_layout_build_tree(bqdoc);
        assert(bqroot != NULL);
        bq = bqroot->first_child;
        assert(bq != NULL);
        assert(fabsf(bq->margin_left - 40.0f) < 0.01f);
        assert(fabsf(bq->margin_right - 40.0f) < 0.01f);

        q_layout_free_tree(bqroot);
        q_document_destroy(bqdoc);
    }

    /* s/del: line-through default */
    {
        static const char del_html[] =
            "<html><body><p><s>s</s><del>d</del></p></body></html>";
        q_document_t *ddoc = q_document_create();
        q_box_t *droot = NULL;
        q_box_t *p, *ic, *kid;
        int strike_count = 0;

        assert(ddoc != NULL);
        assert(q_document_load_html(ddoc, del_html, sizeof(del_html) - 1, NULL) == 0);
        droot = q_layout_build_tree(ddoc);
        assert(droot != NULL);
        p = droot->first_child;
        assert(p != NULL);
        ic = p->first_child;
        assert(ic != NULL && ic->type == Q_BOX_INLINE_CONTAINER);
        for (kid = ic->first_child; kid != NULL; kid = kid->next_sibling) {
            if (kid->is_inline_block
                && (kid->text_decoration & Q_TEXT_DECORATION_LINE_THROUGH) != 0) {
                ++strike_count;
            }
        }
        assert(strike_count == 2);

        q_layout_free_tree(droot);
        q_document_destroy(ddoc);
    }

    /* sup/sub: vertical-align defaults and reduced font-size */
    {
        static const char supsub_html[] =
            "<html><body><p>x<sup>2</sup>H<sub>2</sub></p></body></html>";
        q_document_t *sdoc = q_document_create();
        q_box_t *sroot = NULL;
        q_box_t *p, *ic, *kid;
        int saw_sup = 0, saw_sub = 0;

        assert(sdoc != NULL);
        assert(q_document_load_html(sdoc, supsub_html, sizeof(supsub_html) - 1, NULL) == 0);
        sroot = q_layout_build_tree(sdoc);
        assert(sroot != NULL);
        p = sroot->first_child;
        assert(p != NULL);
        ic = p->first_child;
        assert(ic != NULL && ic->type == Q_BOX_INLINE_CONTAINER);
        for (kid = ic->first_child; kid != NULL; kid = kid->next_sibling) {
            if (!kid->is_inline_block) {
                continue;
            }
            if (kid->vertical_align == Q_VERTICAL_ALIGN_SUPER) {
                saw_sup = 1;
                assert(kid->font_size < 16.0f);
            } else if (kid->vertical_align == Q_VERTICAL_ALIGN_SUB) {
                saw_sub = 1;
                assert(kid->font_size < 16.0f);
            }
        }
        assert(saw_sup);
        assert(saw_sub);

        q_layout_free_tree(sroot);
        q_document_destroy(sdoc);
    }

    /* text-align center/right line placement */
    {
        static const char ta_html[] =
            "<html><body>"
            "<div style='width:200px;text-align:center;'>aaaa</div>"
            "<div style='width:200px;text-align:right;'>aaaa</div>"
            "</body></html>";
        q_document_t *tadoc = q_document_create();
        q_box_t *taroot = NULL;
        q_box_t *center_div;
        q_box_t *right_div;
        q_box_t *center_line;
        q_box_t *right_line;
        q_box_t *center_text;
        q_box_t *right_text;
        float center_offset;
        float right_offset;

        assert(tadoc != NULL);
        assert(q_document_load_html(tadoc, ta_html, sizeof(ta_html) - 1, NULL) == 0);
        taroot = q_layout_build_tree(tadoc);
        assert(taroot != NULL);
        q_layout_measure(taroot, 300.0f, 0.0f);
        q_layout_position(taroot, 0.0f, 0.0f);

        center_div = taroot->first_child;
        assert(center_div != NULL);
        right_div = center_div->next_sibling;
        assert(right_div != NULL);
        assert(right_div->next_sibling == NULL);

        center_line = center_div->first_child->first_child;
        right_line = right_div->first_child->first_child;
        assert(center_line != NULL && right_line != NULL);

        center_text = center_line->first_child;
        right_text = right_line->first_child;
        assert(center_text != NULL && right_text != NULL);

        center_offset = center_text->x - center_line->x;
        right_offset = right_text->x - right_line->x;
        assert(center_offset > 0.0f);
        assert(right_offset > center_offset);

        q_layout_free_tree(taroot);
        q_document_destroy(tadoc);
    }

    /* margin:auto centering + min/max width/height clamping */
    {
        static const char mm_html[] =
            "<html><body>"
            "<div id='auto' style='width:100px;margin:0 auto;'>x</div>"
            "<div id='clampw' style='width:300px;max-width:120px;'>x</div>"
            "<div id='clampminw' style='width:20px;min-width:80px;'>x</div>"
            "<div id='clamph' style='height:10px;min-height:30px;max-height:30px;'>x</div>"
            "</body></html>";
        q_document_t *mmdoc = q_document_create();
        q_box_t *mmroot = NULL;
        q_box_t *auto_div;
        q_box_t *clampw_div;
        q_box_t *clampminw_div;
        q_box_t *clamph_div;

        assert(mmdoc != NULL);
        assert(q_document_load_html(mmdoc, mm_html, sizeof(mm_html) - 1, NULL) == 0);
        mmroot = q_layout_build_tree(mmdoc);
        assert(mmroot != NULL);
        q_layout_measure(mmroot, 300.0f, 0.0f);
        q_layout_position(mmroot, 0.0f, 0.0f);

        auto_div = mmroot->first_child;
        assert(auto_div != NULL);
        clampw_div = auto_div->next_sibling;
        assert(clampw_div != NULL);
        clampminw_div = clampw_div->next_sibling;
        assert(clampminw_div != NULL);
        clamph_div = clampminw_div->next_sibling;
        assert(clamph_div != NULL);

        assert(auto_div->margin_left_auto == 1);
        assert(auto_div->margin_right_auto == 1);
        assert(nearly_equal(auto_div->margin_left, auto_div->margin_right));
        assert(nearly_equal(auto_div->x - mmroot->margin_left, auto_div->margin_left));

        assert(fabsf(clampw_div->width - 120.0f) < 0.01f);
        assert(fabsf(clampminw_div->width - 80.0f) < 0.01f);
        assert(fabsf(clamph_div->height - 30.0f) < 0.01f);

        q_layout_free_tree(mmroot);
        q_document_destroy(mmdoc);
    }

    /* title extraction + backend set_title callback */
    {
        static const char title_html[] =
            "<html><head><title>Stage 3 Title</title></head><body><div>x</div></body></html>";
        static const q_backend_vt_t mock_backend = {
            mock_create_window,
            mock_blit,
            mock_poll_events,
            mock_destroy_window,
            mock_set_title,
        };
        q_document_t *tdoc = q_document_create();
        quanton_ctx_t tctx;
        quanton_view_t tview;

        assert(tdoc != NULL);
        assert(q_document_load_html(tdoc, title_html, sizeof(title_html) - 1, NULL) == 0);

        memset(&tctx, 0, sizeof(tctx));
        memset(&tview, 0, sizeof(tview));
        tctx.backend = &mock_backend;
        tview.ctx = &tctx;
        tview.document = tdoc;
        tview.vp_width = 320;
        tview.vp_height = 200;
        g_last_set_title = NULL;

        q_dom_mark_dirty(&tview, NULL, Q_DIRTY_LAYOUT);
        q_view_update(&tview);
        assert(g_last_set_title != NULL);
        assert(strcmp(g_last_set_title, "Stage 3 Title") == 0);
        assert(tview.layout_root != NULL);
        assert(tview.layout_root->document_title != NULL);
        assert(strcmp(tview.layout_root->document_title, "Stage 3 Title") == 0);

        q_layout_free_tree(tview.layout_root);
        q_document_destroy(tdoc);
    }

    /* ── <a href> visual defaults + on_navigate callback (items 12+13) ──── */
    {
        /* Structural: verify that an <a href="..."> box gets blue colour,
         * underline, and that the href string is stored on the box. */
        static const char a_html[] =
            "<html><body>"
            "<p>before <a href=\"https://example.com\">link text</a> after</p>"
            "</body></html>";
        q_document_t *adoc = q_document_create();
        q_box_t *aroot;
        q_box_t *a_box;

        assert(adoc != NULL);
        assert(q_document_load_html(adoc, a_html, sizeof(a_html) - 1, NULL) == 0);
        aroot = q_layout_build_tree(adoc);
        assert(aroot != NULL);
        /* No measure/position needed: we only inspect the raw box tree before
         * line-wrapping inserts Q_BOX_LINE wrappers between IC and its children. */

        /* Walk tree to find the <a> box (is_inline_block == 1 and href set). */
        a_box = NULL;
        {
            q_box_t *p;   /* p element box */
            q_box_t *ic;  /* anonymous inline container under p */
            q_box_t *ch;
            p = aroot->first_child;
            assert(p != NULL);
            ic = p->first_child;
            assert(ic != NULL && ic->type == Q_BOX_INLINE_CONTAINER);
            for (ch = ic->first_child; ch != NULL; ch = ch->next_sibling) {
                if (ch->href != NULL) {
                    a_box = ch;
                    break;
                }
            }
        }
        assert(a_box != NULL);
        assert(a_box->href != NULL);
        assert(strcmp(a_box->href, "https://example.com") == 0);
        /* UA default: link colour is #0000EE */
        assert(a_box->has_text_color);
        assert(a_box->text_color == 0x0000EEFFu);
        /* UA default: underline */
        assert(a_box->text_decoration & Q_TEXT_DECORATION_UNDERLINE);

        q_layout_free_tree(aroot);
        q_document_destroy(adoc);
    }
    {
        /* Structural: named-anchor click scrolls the view to the target.
         * Use a tall narrow document so section 2 is off-screen (below the
         * viewport) and a simulated left-click on the "#sec2" link causes
         * q_view_scroll_into_view to move scroll_y. */
        static const char nav_html[] =
            "<html><body style=\"margin:0;\">"
            "<p><a href=\"#sec2\">Go to section 2</a></p>"
            /* Filler to push section 2 below the viewport */
            "<div style=\"height:600px;\"></div>"
            "<h2 id=\"sec2\">Section 2</h2>"
            "<p>Content of section 2.</p>"
            "</body></html>";
        static const int VP_W = 400;
        static const int VP_H = 200;
        q_document_t *ndoc;
        quanton_ctx_t nctx;
        quanton_view_t nview;
        q_event_t ev;

        ndoc = q_document_create();
        assert(ndoc != NULL);
        assert(q_document_load_html(ndoc, nav_html, sizeof(nav_html) - 1, NULL) == 0);

        memset(&nctx, 0, sizeof(nctx));
        memset(&nview, 0, sizeof(nview));
        nview.ctx        = &nctx;
        nview.document   = ndoc;
        nview.vp_width   = VP_W;
        nview.vp_height  = VP_H;

        q_dom_mark_dirty(&nview, NULL, Q_DIRTY_LAYOUT);
        q_view_update(&nview);
        assert(nview.layout_root != NULL);

        /* Before click the view is at the top */
        assert(nview.scroll_y == 0.0f);

        /* Simulate a left click at the top-left corner where the link is */
        memset(&ev, 0, sizeof(ev));
        ev.type         = Q_EVENT_MOUSE_CLICK;
        ev.mouse_button = 0;
        ev.mouse_x      = 5;
        ev.mouse_y      = 5;
        q_event_dispatch(&nview, &ev);

        /* After clicking the "#sec2" link, scroll_y must have increased to
         * bring section 2 into view — it is placed after 600 px of filler. */
        assert(nview.scroll_y > 400.0f);

        q_layout_free_tree(nview.layout_root);
        free(nview.framebuffer);
        q_document_destroy(ndoc);
    }
    {
        /* Structural: on_navigate is called for non-anchor hrefs and NOT
         * called for named-anchor hrefs. */
        static const char nested_anchor_html[] =
            "<html><body style=\"margin:0;display:flex;\">"
            "<div style=\"width:200px;height:180px;overflow:auto;\">"
            "<p>left panel</p>"
            "</div>"
            "<div id=\"right\" style=\"width:200px;height:180px;overflow:auto;\">"
            "<p><a href=\"#rsec\">Jump</a></p>"
            "<div style=\"height:700px;\"></div>"
            "<h2 id=\"rsec\">Right Section</h2>"
            "</div>"
            "</body></html>";
        static const char ext_html[] =
            "<html><body style=\"margin:0;\">"
            "<p><a href=\"https://example.com\">External</a></p>"
            "</body></html>";
        q_document_t *rdoc;
        quanton_ctx_t rctx;
        quanton_view_t rview;
        lxb_dom_element_t *right_el;
        q_box_t *right_box;
        q_document_t *edoc;
        quanton_ctx_t ectx;
        quanton_view_t eview;
        q_event_t ev;

        rdoc = q_document_create();
        assert(rdoc != NULL);
        assert(q_document_load_html(rdoc, nested_anchor_html, sizeof(nested_anchor_html) - 1, NULL) == 0);

        memset(&rctx, 0, sizeof(rctx));
        memset(&rview, 0, sizeof(rview));
        rview.ctx       = &rctx;
        rview.document  = rdoc;
        rview.vp_width  = 420;
        rview.vp_height = 220;

        q_dom_mark_dirty(&rview, NULL, Q_DIRTY_LAYOUT);
        q_view_update(&rview);
        assert(rview.layout_root != NULL);

        right_el = q_dom_get_element_by_id(&rview, "right");
        assert(right_el != NULL);
        right_box = find_box_for_dom_node(rview.layout_root,
                                          lxb_dom_interface_node(right_el));
        assert(right_box != NULL);
        assert(right_box->scroll_y == 0.0f);
        assert(rview.scroll_y == 0.0f);

        memset(&ev, 0, sizeof(ev));
        ev.type         = Q_EVENT_MOUSE_CLICK;
        ev.mouse_button = 0;
        ev.mouse_x      = 210;
        ev.mouse_y      = 12;
        q_event_dispatch(&rview, &ev);

        assert(right_box->scroll_y > 400.0f);
        assert(rview.scroll_y == 0.0f);

        q_layout_free_tree(rview.layout_root);
        free(rview.framebuffer);
        q_document_destroy(rdoc);

        edoc = q_document_create();
        assert(edoc != NULL);
        assert(q_document_load_html(edoc, ext_html, sizeof(ext_html) - 1, NULL) == 0);

        memset(&ectx, 0, sizeof(ectx));
        memset(&eview, 0, sizeof(eview));
        eview.ctx        = &ectx;
        eview.document   = edoc;
        eview.vp_width   = 400;
        eview.vp_height  = 200;

        /* Verify that on_navigate is called with the correct href when
         * the user clicks an external link. */
        g_navigate_href = NULL;
        eview.on_navigate = capture_navigate_handler;
        eview.on_navigate_userdata = NULL;

        q_dom_mark_dirty(&eview, NULL, Q_DIRTY_LAYOUT);
        q_view_update(&eview);
        assert(eview.layout_root != NULL);

        memset(&ev, 0, sizeof(ev));
        ev.type         = Q_EVENT_MOUSE_CLICK;
        ev.mouse_button = 0;
        ev.mouse_x      = 5;
        ev.mouse_y      = 5;
        q_event_dispatch(&eview, &ev);

        assert(g_navigate_href != NULL);
        assert(strcmp(g_navigate_href, "https://example.com") == 0);

        /* Also verify that clicking the same link with on_navigate == NULL
         * does not crash. */
        eview.on_navigate = NULL;
        q_event_dispatch(&eview, &ev); /* must not crash */

        q_layout_free_tree(eview.layout_root);
        free(eview.framebuffer);
        q_document_destroy(edoc);
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
