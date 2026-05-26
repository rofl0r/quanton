#include "quanton.h"

#include <assert.h>
#include <stdio.h>

int main(void)
{
    q_document_t *doc;
    q_box_t *root;
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

    assert(q_document_load_html(doc,
                                "<html><body><div>Hello <b>world</b></div></body></html>",
                                54,
                                "file://./tests/input.html")
           == 0);

    root = q_layout_build_tree(doc);
    assert(root != NULL);
    assert(root->first_child != NULL);
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
