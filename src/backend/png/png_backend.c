#define _POSIX_C_SOURCE 200809L

#include "quanton.h"

#include <png.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int png_create_window(quanton_view_t *view, int w, int h, const char *title)
{
    char *path;

    if (title != NULL && title[0] != '\0') {
        path = strdup(title);
    } else {
        path = strdup("output.png");
    }
    if (path == NULL) {
        return -1;
    }

    view->vp_width = w;
    view->vp_height = h;
    view->window_handle = path;
    return 0;
}

static void png_blit(quanton_view_t *view)
{
    const char *path;
    FILE *fp;
    png_structp png_ptr;
    png_infop info_ptr;
    png_bytep *rows;
    int y;

    if (view == NULL || view->framebuffer == NULL
        || view->vp_width <= 0 || view->vp_height <= 0) {
        return;
    }

    path = (view->window_handle != NULL)
             ? (const char *) view->window_handle
             : "output.png";

    fp = fopen(path, "wb");
    if (fp == NULL) {
        return;
    }

    png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (png_ptr == NULL) {
        fclose(fp);
        return;
    }

    info_ptr = png_create_info_struct(png_ptr);
    if (info_ptr == NULL) {
        png_destroy_write_struct(&png_ptr, NULL);
        fclose(fp);
        return;
    }

    if (setjmp(png_jmpbuf(png_ptr))) {
        png_destroy_write_struct(&png_ptr, &info_ptr);
        fclose(fp);
        return;
    }

    png_init_io(png_ptr, fp);
    png_set_IHDR(png_ptr, info_ptr,
                 (png_uint_32) view->vp_width,
                 (png_uint_32) view->vp_height,
                 8, PNG_COLOR_TYPE_RGBA,
                 PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_DEFAULT,
                 PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png_ptr, info_ptr);

    rows = (png_bytep *) malloc((size_t) view->vp_height * sizeof(png_bytep));
    if (rows == NULL) {
        png_destroy_write_struct(&png_ptr, &info_ptr);
        fclose(fp);
        return;
    }

    for (y = 0; y < view->vp_height; ++y) {
        rows[y] = view->framebuffer
                  + (size_t) y * (size_t) view->vp_width * 4u;
    }

    png_write_image(png_ptr, rows);
    png_write_end(png_ptr, NULL);

    free(rows);
    png_destroy_write_struct(&png_ptr, &info_ptr);
    fclose(fp);
}

static void png_poll_events(quanton_view_t *view)
{
    q_event_t ev;

    if (view == NULL) {
        return;
    }

    memset(&ev, 0, sizeof(ev));
    ev.type = Q_EVENT_CLOSE;

    view->should_close = 1;
    q_event_dispatch(view, &ev);
}

static void png_destroy_window(quanton_view_t *view)
{
    if (view == NULL) {
        return;
    }
    free(view->window_handle);
    view->window_handle = NULL;
}

static void png_set_title(quanton_view_t *view, const char *title)
{
    (void) view;
    (void) title;
}

const q_backend_vt_t q_backend_png = {
    png_create_window,
    NULL,
    png_blit,
    png_poll_events,
    png_destroy_window,
    png_set_title,
};
