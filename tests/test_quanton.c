#include "quanton.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>

static int nearly_equal(float a, float b)
{
    return fabsf(a - b) < 0.01f;
}

int main(void)
{
    static const char html[] =
        "<html><body><div>Hello</div><div><p>world</p></div></body></html>";
    q_document_t *doc;
    q_box_t *root;
    q_box_t *first_block;
    q_box_t *second_block;
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

    first_text = first_block->first_child;
    assert(first_text != NULL);
    assert(first_text->type == Q_BOX_TEXT);
    assert(first_text->width > 0.0f);
    assert(first_text->height > 0.0f);
    if (first_text->run != NULL) {
        assert(first_text->run->count > 0);
    }

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
