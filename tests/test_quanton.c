#include "quanton.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    q_document_t *doc;
    q_font_cache_t *cache;
    q_font_t *font;
    q_shaped_run_t *run;
    uint8_t *buf;
    size_t len = 0;

    buf = q_resource_load("file:///tmp/workspace/rofl0r/quanton/IMPLEMENTATION_PLAN.md", &len);
    assert(buf != NULL);
    assert(len > 0);
    q_resource_free(buf);

    doc = q_document_create();
    assert(doc != NULL);
    assert(q_document_load_html(doc, "<html><body>ok</body></html>", 28, "file:///tmp/demo.html") == 0);
    q_document_destroy(doc);

    cache = q_font_cache_create();
    assert(cache != NULL);

    font = q_font_load(cache, "sans-serif", "/usr/share/fonts/dejavu/DejaVuSans.ttf", 16.0f, 400);
    if (font == NULL) {
        static const unsigned char dummy_font[] = {0x00, 0x01, 0x02, 0x03};
        font = q_font_load_mem(cache, "sans-serif", dummy_font, sizeof(dummy_font), 16.0f, 400);
    }
    assert(font != NULL);
    assert(q_font_measure(font, "hello", 5) > 0.0f);

    run = q_font_shape_run(font, "hello", 5);
    assert(run != NULL);
    assert(run->count == 5);
    q_shaped_run_free(run);

    q_font_cache_destroy(cache);

    puts("ok");
    return 0;
}
