#define _POSIX_C_SOURCE 200809L

#include "quanton.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *q_resource_parse_file_url(const char *url)
{
    static const char scheme[] = "file://";
    const char *path;

    if (url == NULL) {
        return NULL;
    }

    if (strncmp(url, scheme, sizeof(scheme) - 1) != 0) {
        return NULL;
    }

    path = url + (sizeof(scheme) - 1);
    if (strncmp(path, "localhost/", 10) == 0) {
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

static int q_url_has_scheme(const char *url)
{
    const char *sep;

    if (url == NULL) {
        return 0;
    }

    sep = strstr(url, "://");
    return sep != NULL && sep != url;
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
    size_t extra = 0;
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

char *q_url_resolve(const char *base_url, const char *ref)
{
    const char *base_path;
    char *base_dir;
    char *url;
    size_t base_dir_len;
    size_t ref_len;
    size_t prefix_len = sizeof("file://") - 1u;
    size_t extra = 0;

    if (ref == NULL || ref[0] == '\0') {
        return NULL;
    }

    if (q_url_has_scheme(ref)) {
        return strdup(ref);
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

uint8_t *q_resource_load(const char *url, size_t *out_len)
{
    const char *path = q_resource_parse_file_url(url);
    FILE *fp;
    long file_len;
    uint8_t *buf;
    size_t read_len;

    if (path == NULL) {
        return NULL;
    }

    fp = fopen(path, "rb");
    if (fp == NULL) {
        return NULL;
    }

    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return NULL;
    }

    file_len = ftell(fp);
    if (file_len < 0 || fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return NULL;
    }

    buf = (uint8_t *) malloc((size_t) file_len + 1);
    if (buf == NULL) {
        fclose(fp);
        return NULL;
    }

    read_len = fread(buf, 1, (size_t) file_len, fp);
    fclose(fp);

    if (read_len != (size_t) file_len) {
        free(buf);
        return NULL;
    }

    buf[read_len] = '\0';

    if (out_len != NULL) {
        *out_len = read_len;
    }

    return buf;
}

void q_resource_free(uint8_t *buf)
{
    free(buf);
}
