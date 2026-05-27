#define _POSIX_C_SOURCE 200809L
#include "quanton.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define FLOAT_TOLERANCE 0.01f
#define TEST_WIDTH 800
#define TEST_HEIGHT 600

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
    q_paint_box(root);

    memset(&ctx, 0, sizeof(ctx));
    memset(&view, 0, sizeof(view));
    ctx.backend = &q_backend_png;
    view.ctx = &ctx;
    view.layout_root = root;

    assert(ctx.backend->create_window(&view, width, height, output_png) == 0);
    q_composite_frame(&view);
    assert(framebuffer_has_ink(view.framebuffer, view.vp_width, view.vp_height));
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

int main(void)
{
    static const char html[] =
        "<html><body><div>Hello</div><div><p>world</p></div></body></html>";
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

    buf = q_resource_load("file://./IMPLEMENTATION_PLAN.md", &len);
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
    assert_pixel_rgba(first_block->tile, first_block->tile_w, first_block->tile_h, 3, 3, 16, 32, 48, 255);

    assert_pixel_rgba(root->tile, root->tile_w, root->tile_h, 3, 3, 16, 32, 48, 255);

#if defined(QUANTON_BACKEND_PNG) || defined(QUANTON_BACKEND_X11) || defined(QUANTON_BACKEND_SDL2)
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
#else
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
#endif
    }
#endif

    q_layout_free_tree(root);

    cache = q_font_cache_create();
    assert(cache != NULL);

    font = q_font_load(cache, "sans-serif", "/usr/share/fonts/dejavu/DejaVuSans.ttf", 16.0f, 400);
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
