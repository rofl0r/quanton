#include "quanton.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FLOAT_TOLERANCE 0.01f

static int nearly_equal(float a, float b)
{
    return fabsf(a - b) < FLOAT_TOLERANCE;
}

static void assert_pixel_rgba(const uint8_t *pixels, int width, int height, int x, int y,
                              uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    assert(pixels != NULL);
    assert(width > 0);
    assert(x >= 0);
    assert(y >= 0);
    assert(x < width);
    assert(y < height);
    size_t idx = (size_t) (y * width + x) * 4u;
    assert(pixels[idx + 0] == r);
    assert(pixels[idx + 1] == g);
    assert(pixels[idx + 2] == b);
    assert(pixels[idx + 3] == a);
}

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

    /* After measure, text nodes are inside anonymous inline containers / line boxes */
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
        bview.vp_width = 320;
        bview.vp_height = 240;

#if defined(QUANTON_BACKEND_PNG)
        assert(bctx.backend->create_window(&bview, 320, 240, "output.png") == 0);
        q_composite_frame(&bview);
        assert(bview.framebuffer != NULL);
        bctx.backend->blit(&bview);
        {
            FILE *fp = fopen("output.png", "rb");
            assert(fp != NULL);
            fclose(fp);
        }
        free(bview.framebuffer);
        bctx.backend->destroy_window(&bview);
#else
        /* X11/SDL2: attempt backend test; may skip gracefully if no display */
        if (bctx.backend->create_window(&bview, 320, 240, "quanton-test") == 0) {
            q_composite_frame(&bview);
            if (bview.framebuffer != NULL) {
                bctx.backend->blit(&bview);
                free(bview.framebuffer);
                bview.framebuffer = NULL;
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
