#define _POSIX_C_SOURCE 200809L

#include "quanton.h"

#include "lexbor/css/css.h"
#include "lexbor/dom/interfaces/element.h"
#include "lexbor/dom/interfaces/node.h"
#include "lexbor/html/interfaces/document.h"
#include "lexbor/style/html/interfaces/document.h"
#include "lexbor/tag/const.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

struct q_document {
    lxb_html_document_t *document;
    char *html;
    size_t html_len;
    char *base_url;
};

static int q_rel_has_stylesheet_token(const lxb_char_t *rel, size_t rel_len)
{
    size_t i = 0u;

    while (i < rel_len) {
        size_t start;
        size_t len;
        while (i < rel_len && isspace((unsigned char) rel[i])) {
            ++i;
        }
        start = i;
        while (i < rel_len && !isspace((unsigned char) rel[i])) {
            ++i;
        }
        len = i - start;
        if (len == sizeof("stylesheet") - 1u) {
            size_t j;
            for (j = 0u; j < len; ++j) {
                if (tolower((unsigned char) rel[start + j])
                    != (unsigned char) "stylesheet"[j])
                {
                    break;
                }
            }
            if (j == len) {
                return 1;
            }
        }
    }

    return 0;
}

static int q_document_load_link_stylesheet(lxb_html_document_t *document,
                                           const char *base_url,
                                           lxb_dom_element_t *element)
{
    lxb_dom_document_css_t *css = document->dom_document.css;
    const lxb_char_t *href;
    const lxb_char_t *rel;
    size_t href_len = 0u;
    size_t rel_len = 0u;
    char *href_str;
    char *url;
    q_resource_t resource;
    lxb_css_stylesheet_t *stylesheet;
    lxb_status_t status;
    int result = 0;

    rel = lxb_dom_element_get_attribute(element, (const lxb_char_t *) "rel",
                                        sizeof("rel") - 1u, &rel_len);
    if (rel == NULL || !q_rel_has_stylesheet_token(rel, rel_len)
        || lxb_dom_element_has_attribute(element, (const lxb_char_t *) "disabled",
                                         sizeof("disabled") - 1u))
    {
        return 0;
    }

    href = lxb_dom_element_get_attribute(element, (const lxb_char_t *) "href",
                                         sizeof("href") - 1u, &href_len);
    if (href == NULL || href_len == 0u) {
        return 0;
    }

    href_str = (char *) malloc(href_len + 1u);
    if (href_str == NULL) {
        return -1;
    }
    memcpy(href_str, href, href_len);
    href_str[href_len] = '\0';
    url = q_url_resolve(base_url, href_str);
    free(href_str);
    if (url == NULL) {
        return -1;
    }

    if (!q_resource_open(url, &resource)) {
        free(url);
        return 0;
    }
    free(url);

    stylesheet = lxb_css_stylesheet_create(css->memory);
    if (stylesheet == NULL) {
        q_resource_close(&resource);
        return -1;
    }

    status = lxb_css_stylesheet_parse(stylesheet, css->parser,
                                      resource.data, resource.size);
    q_resource_close(&resource);
    if (status != LXB_STATUS_OK || stylesheet->root == NULL) {
        (void) lxb_css_stylesheet_destroy(stylesheet, false);
        return status == LXB_STATUS_OK ? 0 : -1;
    }

    status = lxb_html_document_stylesheet_attach(document, stylesheet);
    if (status != LXB_STATUS_OK) {
        (void) lxb_css_stylesheet_destroy(stylesheet, false);
        return -1;
    }

    result = 1;
    return result;
}

static int q_document_load_link_stylesheets(lxb_html_document_t *document,
                                            const char *base_url,
                                            lxb_dom_node_t *node)
{
    lxb_dom_node_t *child;

    if (node->type == LXB_DOM_NODE_TYPE_ELEMENT
        && lxb_dom_node_tag_id(node) == LXB_TAG_LINK
        && q_document_load_link_stylesheet(document, base_url,
                                           lxb_dom_interface_element(node)) < 0)
    {
        return -1;
    }

    for (child = node->first_child; child != NULL; child = child->next) {
        if (q_document_load_link_stylesheets(document, base_url, child) != 0) {
            return -1;
        }
    }
    return 0;
}

q_document_t *q_document_create(void)
{
    return (q_document_t *) calloc(1, sizeof(q_document_t));
}

void q_document_destroy(q_document_t *doc)
{
    if (doc == NULL) {
        return;
    }

    if (doc->document != NULL) {
        doc->document = lxb_html_document_destroy(doc->document);
    }

    free(doc->html);
    free(doc->base_url);
    free(doc);
}

int q_document_load_html(q_document_t *doc, const char *html, size_t len, const char *base_url)
{
    lxb_html_document_t *new_document;
    char *new_html;
    char *new_base = NULL;

    if (doc == NULL || html == NULL) {
        return -1;
    }

    new_document = lxb_html_document_create();
    if (new_document == NULL) {
        return -1;
    }

    if (lxb_html_document_parse(new_document, (const lxb_char_t *) html, len) != LXB_STATUS_OK) {
        (void) lxb_html_document_destroy(new_document);
        return -1;
    }

    if (q_document_load_link_stylesheets(new_document, base_url,
                                          lxb_dom_interface_node(new_document)) != 0)
    {
        (void) lxb_html_document_destroy(new_document);
        return -1;
    }

    new_html = (char *) malloc(len + 1);
    if (new_html == NULL) {
        (void) lxb_html_document_destroy(new_document);
        return -1;
    }

    memcpy(new_html, html, len);
    new_html[len] = '\0';

    if (base_url != NULL) {
        new_base = strdup(base_url);
        if (new_base == NULL) {
            free(new_html);
            (void) lxb_html_document_destroy(new_document);
            return -1;
        }
    }

    if (doc->document != NULL) {
        doc->document = lxb_html_document_destroy(doc->document);
    }

    free(doc->html);
    free(doc->base_url);

    doc->document = new_document;
    doc->html = new_html;
    doc->html_len = len;
    doc->base_url = new_base;

    return 0;
}

int q_document_load_url(q_document_t *doc, const char *url)
{
    q_resource_t resource;
    int rc;

    if (doc == NULL || url == NULL) {
        return -1;
    }

    if (!q_resource_open(url, &resource)) {
        return -1;
    }

    rc = q_document_load_html(doc, (const char *) resource.data, resource.size, url);
    q_resource_close(&resource);

    return rc;
}

lxb_html_document_t *q_document_handle(q_document_t *doc)
{
    if (doc == NULL) {
        return NULL;
    }

    return doc->document;
}

const char *q_document_base_url(const q_document_t *doc)
{
    if (doc == NULL) {
        return NULL;
    }

    return doc->base_url;
}

const lxb_css_rule_declaration_t *q_document_get_computed_style(const q_document_t *doc,
                                                                const lxb_dom_node_t *node)
{
    (void) doc;
    (void) node;
    return NULL;
}
