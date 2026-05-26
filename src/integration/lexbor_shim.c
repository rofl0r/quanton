#include "quanton.h"

#include <stdlib.h>
#include <string.h>

struct q_document {
    lxb_html_document_t *document;
    char *html;
    size_t html_len;
    char *base_url;
};

q_document_t *q_document_create(void)
{
    q_document_t *doc = (q_document_t *) calloc(1, sizeof(*doc));
    return doc;
}

void q_document_destroy(q_document_t *doc)
{
    if (doc == NULL) {
        return;
    }

    free(doc->html);
    free(doc->base_url);
    free(doc);
}

int q_document_load_html(q_document_t *doc, const char *html, size_t len, const char *base_url)
{
    char *new_html;
    char *new_base = NULL;

    if (doc == NULL || html == NULL) {
        return -1;
    }

    new_html = (char *) malloc(len + 1);
    if (new_html == NULL) {
        return -1;
    }

    memcpy(new_html, html, len);
    new_html[len] = '\0';

    if (base_url != NULL) {
        size_t base_len = strlen(base_url);
        new_base = (char *) malloc(base_len + 1);
        if (new_base == NULL) {
            free(new_html);
            return -1;
        }

        memcpy(new_base, base_url, base_len + 1);
    }

    free(doc->html);
    free(doc->base_url);

    doc->html = new_html;
    doc->html_len = len;
    doc->base_url = new_base;

    return 0;
}

int q_document_load_url(q_document_t *doc, const char *url)
{
    uint8_t *buf;
    size_t len = 0;
    int rc;

    if (doc == NULL || url == NULL) {
        return -1;
    }

    buf = q_resource_load(url, &len);
    if (buf == NULL) {
        return -1;
    }

    rc = q_document_load_html(doc, (const char *) buf, len, url);
    q_resource_free(buf);

    return rc;
}

lxb_html_document_t *q_document_handle(q_document_t *doc)
{
    if (doc == NULL) {
        return NULL;
    }

    return doc->document;
}

const lxb_css_rule_declaration_t *q_document_get_computed_style(const q_document_t *doc,
                                                                const lxb_dom_node_t *node)
{
    (void) doc;
    (void) node;
    return NULL;
}
