#define _POSIX_C_SOURCE 200809L
#define STB_IMAGE_IMPLEMENTATION

#include "quanton.h"

#include "stb_image/stb_image.h"

#include <stdlib.h>
#include <string.h>

struct q_image {
    char *url;
    uint8_t *pixels;
    int width;
    int height;
    size_t ref_count;
    struct q_image *next;
};

static q_image_t *q_image_cache_head;

static q_image_t *q_image_find(const char *url)
{
    q_image_t *image;

    for (image = q_image_cache_head; image != NULL; image = image->next) {
        if (strcmp(image->url, url) == 0) {
            return image;
        }
    }

    return NULL;
}

q_image_t *q_image_load_url(const char *url)
{
    q_image_t *image;
    uint8_t *encoded;
    size_t encoded_len = 0;
    int width;
    int height;
    int components;
    stbi_uc *decoded;

    if (url == NULL) {
        return NULL;
    }

    image = q_image_find(url);
    if (image != NULL) {
        ++image->ref_count;
        return image;
    }

    encoded = q_resource_load(url, &encoded_len);
    if (encoded == NULL) {
        return NULL;
    }

    decoded = stbi_load_from_memory(encoded, (int) encoded_len, &width, &height, &components, 4);
    q_resource_free(encoded);
    if (decoded == NULL) {
        return NULL;
    }
    (void) components;

    image = (q_image_t *) calloc(1, sizeof(*image));
    if (image == NULL) {
        stbi_image_free(decoded);
        return NULL;
    }

    image->url = strdup(url);
    if (image->url == NULL) {
        stbi_image_free(decoded);
        free(image);
        return NULL;
    }

    image->pixels = (uint8_t *) decoded;
    image->width = width;
    image->height = height;
    image->ref_count = 1u;
    image->next = q_image_cache_head;
    q_image_cache_head = image;

    return image;
}

void q_image_release(q_image_t *image)
{
    q_image_t **link;

    if (image == NULL) {
        return;
    }

    if (image->ref_count > 1u) {
        --image->ref_count;
        return;
    }

    for (link = &q_image_cache_head; *link != NULL; link = &(*link)->next) {
        if (*link == image) {
            *link = image->next;
            break;
        }
    }

    stbi_image_free(image->pixels);
    free(image->url);
    free(image);
}

const uint8_t *q_image_pixels(const q_image_t *image)
{
    if (image == NULL) {
        return NULL;
    }

    return image->pixels;
}

int q_image_width(const q_image_t *image)
{
    if (image == NULL) {
        return 0;
    }

    return image->width;
}

int q_image_height(const q_image_t *image)
{
    if (image == NULL) {
        return 0;
    }

    return image->height;
}
