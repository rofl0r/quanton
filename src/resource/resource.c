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
