#define _POSIX_C_SOURCE 200809L

#include "quanton.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

typedef struct q_resource_backend {
    char *scheme;
    q_resource_backend_load_fn load;
    void *userdata;
    struct q_resource_backend *next;
} q_resource_backend_t;

static q_resource_backend_t *q_resource_backends;

static size_t q_url_scheme_length(const char *url)
{
    const char *p;

    if (url == NULL || !isalpha((unsigned char) url[0])) {
        return 0u;
    }

    for (p = url + 1; *p != '\0'; ++p) {
        if (*p == ':') {
            return (size_t) (p - url);
        }
        if (!isalnum((unsigned char) *p) && *p != '+' && *p != '-' && *p != '.') {
            return 0u;
        }
    }

    return 0u;
}

static int q_scheme_equal_n(const char *scheme, size_t scheme_len,
                            const char *name)
{
    size_t i;

    if (strlen(name) != scheme_len) {
        return 0;
    }
    for (i = 0; i < scheme_len; ++i) {
        if (tolower((unsigned char) scheme[i])
            != tolower((unsigned char) name[i]))
        {
            return 0;
        }
    }
    return 1;
}

static const char *q_resource_parse_file_url(const char *url)
{
    static const char scheme[] = "file://";
    const char *path;

    if (url == NULL) {
        return NULL;
    }

    if (strncasecmp(url, scheme, sizeof(scheme) - 1u) != 0) {
        return NULL;
    }

    path = url + (sizeof(scheme) - 1u);
    if (strncmp(path, "localhost/", 10u) == 0) {
        path += 9;
    }

    if (path[0] == '.' && (path[1] == '/' || path[1] == '\0')) {
        return path;
    }

    if (path[0] != '/') {
        return NULL;
    }

    return path;
}

static int q_file_path_needs_dot_prefix(const char *path)
{
    if (path == NULL || path[0] == '\0') {
        return 1;
    }

    if (path[0] == '/') {
        return 0;
    }

    return !(path[0] == '.' && (path[1] == '/' || path[1] == '\0'));
}

static char *q_file_url_build(const char *path)
{
    size_t path_len;
    size_t prefix_len = sizeof("file://") - 1u;
    size_t extra = 0u;
    char *url;

    if (path == NULL) {
        return NULL;
    }

    path_len = strlen(path);
    if (q_file_path_needs_dot_prefix(path)) {
        extra = 2u;
    }

    url = (char *) malloc(prefix_len + extra + path_len + 1u);
    if (url == NULL) {
        return NULL;
    }

    memcpy(url, "file://", prefix_len);
    if (extra != 0u) {
        memcpy(url + prefix_len, "./", 2u);
    }
    memcpy(url + prefix_len + extra, path, path_len + 1u);

    return url;
}

static char *q_path_dirname_dup(const char *path)
{
    const char *slash;
    size_t len;
    char *dir;

    if (path == NULL) {
        return NULL;
    }

    slash = strrchr(path, '/');
    if (slash == NULL) {
        dir = (char *) malloc(3u);
        if (dir == NULL) {
            return NULL;
        }

        memcpy(dir, "./", 3u);
        return dir;
    }

    len = (size_t) (slash - path) + 1u;
    dir = (char *) malloc(len + 1u);
    if (dir == NULL) {
        return NULL;
    }

    memcpy(dir, path, len);
    dir[len] = '\0';
    return dir;
}

static char *q_url_normalize_path_dup(const char *path)
{
    size_t path_len = strlen(path);
    size_t *segments;
    size_t segment_count = 0u;
    size_t output_len = 1u;
    size_t i = 1u;
    int trailing_slash = path_len > 1u && path[path_len - 1u] == '/';
    char *output;

    output = (char *) malloc(path_len + 2u);
    segments = (size_t *) malloc((path_len + 1u) * sizeof(*segments));
    if (output == NULL || segments == NULL) {
        free(output);
        free(segments);
        return NULL;
    }
    output[0] = '/';

    while (i <= path_len) {
        size_t start = i;
        size_t segment_len;
        while (i < path_len && path[i] != '/') {
            ++i;
        }
        segment_len = i - start;
        if (segment_len == 1u && path[start] == '.') {
            if (i == path_len) {
                trailing_slash = 1;
            }
        } else if (segment_len == 2u && path[start] == '.'
                   && path[start + 1u] == '.')
        {
            if (segment_count > 0u) {
                output_len = segments[--segment_count];
            }
            if (i == path_len) {
                trailing_slash = 1;
            }
        } else if (segment_len != 0u) {
            if (output_len > 1u) {
                output[output_len++] = '/';
            }
            segments[segment_count++] = output_len;
            memcpy(output + output_len, path + start, segment_len);
            output_len += segment_len;
        }
        ++i;
    }

    if (trailing_slash && output_len > 1u) {
        output[output_len++] = '/';
    }
    output[output_len] = '\0';
    free(segments);
    return output;
}

static char *q_generic_url_resolve(const char *base_url, size_t scheme_len,
                                   const char *ref)
{
    const char *authority;
    const char *path;
    const char *authority_end;
    const char *slash;
    size_t base_len = strlen(base_url);
    size_t ref_len = strlen(ref);
    size_t origin_len;
    size_t directory_len;
    size_t prefix_len;
    char *normalized_path;
    char *url;

    if (base_len < scheme_len + 3u
        || base_url[scheme_len + 1u] != '/'
        || base_url[scheme_len + 2u] != '/')
    {
        return NULL;
    }

    if (ref[0] == '/' && ref[1] == '/') {
        url = (char *) malloc(scheme_len + 1u + ref_len + 1u);
        if (url == NULL) {
            return NULL;
        }
        memcpy(url, base_url, scheme_len);
        url[scheme_len] = ':';
        memcpy(url + scheme_len + 1u, ref, ref_len + 1u);
        return url;
    }

    authority = base_url + scheme_len + 3u;
    authority_end = strchr(authority, '/');
    origin_len = authority_end != NULL
        ? (size_t) (authority_end - base_url)
        : base_len;
    path = authority_end != NULL ? authority_end : "/";

    if (ref[0] == '/') {
        directory_len = 0u;
        prefix_len = origin_len;
    } else {
        slash = strrchr(path, '/');
        directory_len = slash != NULL ? (size_t) (slash - path) + 1u : 1u;
        prefix_len = origin_len + directory_len;
    }

    url = (char *) malloc(prefix_len + ref_len + 1u);
    if (url == NULL) {
        return NULL;
    }

    memcpy(url, base_url, origin_len);
    if (ref[0] != '/') {
        if (authority_end == NULL) {
            url[origin_len] = '/';
        } else {
            memcpy(url + origin_len, path, directory_len);
        }
    }
    memcpy(url + prefix_len, ref, ref_len + 1u);

    normalized_path = q_url_normalize_path_dup(url + origin_len);
    if (normalized_path == NULL) {
        free(url);
        return NULL;
    }
    {
        size_t normalized_len = strlen(normalized_path);
        char *normalized_url = (char *) malloc(origin_len + normalized_len + 1u);
        if (normalized_url == NULL) {
            free(normalized_path);
            free(url);
            return NULL;
        }
        memcpy(normalized_url, url, origin_len);
        memcpy(normalized_url + origin_len, normalized_path, normalized_len + 1u);
        free(normalized_path);
        free(url);
        return normalized_url;
    }
}

char *q_url_resolve(const char *base_url, const char *ref)
{
    const char *base_path;
    size_t scheme_len;
    char *base_dir;
    char *url;
    size_t base_dir_len;
    size_t ref_len;
    size_t prefix_len = sizeof("file://") - 1u;
    size_t extra = 0u;

    if (ref == NULL || ref[0] == '\0') {
        return NULL;
    }

    if (q_url_scheme_length(ref) != 0u) {
        return strdup(ref);
    }

    scheme_len = q_url_scheme_length(base_url);
    if (scheme_len != 0u && !q_scheme_equal_n(base_url, scheme_len, "file")) {
        if (ref[0] == '/') {
            return q_generic_url_resolve(base_url, scheme_len, ref);
        }
        if (base_url[scheme_len + 1u] == '/'
            && base_url[scheme_len + 2u] == '/')
        {
            return q_generic_url_resolve(base_url, scheme_len, ref);
        }
    }

    if (ref[0] == '/') {
        return q_file_url_build(ref);
    }

    base_path = q_resource_parse_file_url(base_url);
    if (base_path == NULL) {
        return q_file_url_build(ref);
    }

    base_dir = q_path_dirname_dup(base_path);
    if (base_dir == NULL) {
        return NULL;
    }

    base_dir_len = strlen(base_dir);
    ref_len = strlen(ref);
    if (base_dir_len == 0u || q_file_path_needs_dot_prefix(base_dir)) {
        extra = 2u;
    }

    url = (char *) malloc(prefix_len + extra + base_dir_len + ref_len + 1u);
    if (url == NULL) {
        free(base_dir);
        return NULL;
    }

    memcpy(url, "file://", prefix_len);
    if (extra != 0u) {
        memcpy(url + prefix_len, "./", 2u);
    }
    memcpy(url + prefix_len + extra, base_dir, base_dir_len);
    memcpy(url + prefix_len + extra + base_dir_len, ref, ref_len + 1u);

    free(base_dir);
    return url;
}

static void q_resource_heap_release(void *userdata, const uint8_t *data)
{
    (void) userdata;
    free((void *) data);
}

static int q_resource_load_file(const char *url, q_resource_t *resource)
{
    const char *path = q_resource_parse_file_url(url);
    FILE *fp;
    long file_len;
    uint8_t *buf;
    size_t read_len;

    if (path == NULL) {
        return 0;
    }

    fp = fopen(path, "rb");
    if (fp == NULL) {
        return 0;
    }

    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return 0;
    }

    file_len = ftell(fp);
    if (file_len < 0 || fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return 0;
    }

    if ((uintmax_t) file_len > (uintmax_t) SIZE_MAX - 1u) {
        fclose(fp);
        return 0;
    }
    buf = (uint8_t *) malloc((size_t) file_len + 1u);
    if (buf == NULL) {
        fclose(fp);
        return 0;
    }

    read_len = fread(buf, 1u, (size_t) file_len, fp);
    fclose(fp);
    if (read_len != (size_t) file_len) {
        free(buf);
        return 0;
    }

    buf[read_len] = '\0';
    resource->data = buf;
    resource->size = read_len;
    resource->release = q_resource_heap_release;
    resource->userdata = NULL;
    return 1;
}

int q_resource_backend_register(const char *scheme,
                               q_resource_backend_load_fn load,
                               void *userdata)
{
    size_t scheme_len;
    size_t i;
    q_resource_backend_t *backend;
    char *normalized;

    if (scheme == NULL || load == NULL) {
        return -1;
    }

    scheme_len = strlen(scheme);
    if (scheme_len == 0u || !isalpha((unsigned char) scheme[0])) {
        return -1;
    }
    for (i = 1u; i < scheme_len; ++i) {
        if (!isalnum((unsigned char) scheme[i])
            && scheme[i] != '+' && scheme[i] != '-' && scheme[i] != '.')
        {
            return -1;
        }
    }

    normalized = strdup(scheme);
    if (normalized == NULL) {
        return -1;
    }
    for (i = 0u; i < scheme_len; ++i) {
        normalized[i] = (char) tolower((unsigned char) normalized[i]);
    }

    for (backend = q_resource_backends; backend != NULL; backend = backend->next) {
        if (strcmp(backend->scheme, normalized) == 0) {
            free(normalized);
            backend->load = load;
            backend->userdata = userdata;
            return 0;
        }
    }

    backend = (q_resource_backend_t *) calloc(1u, sizeof(*backend));
    if (backend == NULL) {
        free(normalized);
        return -1;
    }
    backend->scheme = normalized;
    backend->load = load;
    backend->userdata = userdata;
    backend->next = q_resource_backends;
    q_resource_backends = backend;
    return 0;
}

int q_resource_open(const char *url, q_resource_t *resource)
{
    q_resource_backend_t *backend;
    size_t scheme_len;

    if (resource == NULL) {
        return 0;
    }
    memset(resource, 0, sizeof(*resource));
    scheme_len = q_url_scheme_length(url);
    if (scheme_len == 0u || url[scheme_len + 1u] != '/'
        || url[scheme_len + 2u] != '/')
    {
        return 0;
    }

    if (q_scheme_equal_n(url, scheme_len, "file")) {
        return q_resource_load_file(url, resource);
    }

    for (backend = q_resource_backends; backend != NULL; backend = backend->next) {
        if (q_scheme_equal_n(url, scheme_len, backend->scheme)) {
            if (backend->load(backend->userdata, url, resource)) {
                return resource->data != NULL || resource->size == 0u;
            }
            q_resource_close(resource);
            return 0;
        }
    }

    return 0;
}

void q_resource_close(q_resource_t *resource)
{
    if (resource == NULL) {
        return;
    }
    if (resource->release != NULL) {
        resource->release(resource->userdata, resource->data);
    }
    memset(resource, 0, sizeof(*resource));
}

uint8_t *q_resource_load(const char *url, size_t *out_len)
{
    q_resource_t resource;
    uint8_t *copy;

    if (!q_resource_open(url, &resource)) {
        return NULL;
    }
    if (resource.size == SIZE_MAX) {
        q_resource_close(&resource);
        return NULL;
    }

    copy = (uint8_t *) malloc(resource.size + 1u);
    if (copy == NULL) {
        q_resource_close(&resource);
        return NULL;
    }
    if (resource.size != 0u) {
        memcpy(copy, resource.data, resource.size);
    }
    copy[resource.size] = '\0';
    if (out_len != NULL) {
        *out_len = resource.size;
    }
    q_resource_close(&resource);
    return copy;
}

void q_resource_free(uint8_t *buf)
{
    free(buf);
}
