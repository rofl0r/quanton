/*
 * SDL2 backend for quanton.
 *
 * Default path renders directly from box self-tiles using one SDL texture per
 * box and a small viewport-aware cache. A legacy framebuffer blit path remains
 * for callers that invoke backend->blit() directly.
 */

#include "quanton.h"

#include <SDL2/SDL.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define Q_SDL2_CACHE_MARGIN_PX 256
#define Q_SDL2_CACHE_TTL_FRAMES 180u

typedef struct q_sdl2_tex_entry {
    const q_box_t *box;
    SDL_Texture *texture;
    int w;
    int h;
    uint64_t uploaded_revision;
    uint64_t last_used_frame;
    struct q_sdl2_tex_entry *next;
} q_sdl2_tex_entry_t;

typedef struct {
    SDL_Window   *window;
    SDL_Renderer *renderer;
    SDL_Texture  *texture;  /* fallback framebuffer texture */
    int           tex_w;
    int           tex_h;
    q_sdl2_tex_entry_t *cache;
    uint64_t frame_id;
    const q_box_t *last_root;
} q_sdl2_win_t;

static void sdl2_dispatch(quanton_view_t *view, q_event_t *ev)
{
    q_event_dispatch(view, ev);
}

static uint32_t sdl2_mod(SDL_Keymod mod)
{
    return ((mod & KMOD_SHIFT) ? 1u : 0u) |
           ((mod & KMOD_CTRL)  ? 2u : 0u) |
           ((mod & KMOD_ALT)   ? 4u : 0u);
}

static int q_box_scrolls_x(const q_box_t *box)
{
    return box != NULL
        && (box->overflow_x == Q_OVERFLOW_SCROLL || box->overflow_x == Q_OVERFLOW_AUTO);
}

static int q_box_scrolls_y(const q_box_t *box)
{
    return box != NULL
        && (box->overflow_y == Q_OVERFLOW_SCROLL || box->overflow_y == Q_OVERFLOW_AUTO);
}

static int q_box_overflow_clips(q_overflow_type_t overflow)
{
    return overflow == Q_OVERFLOW_HIDDEN
        || overflow == Q_OVERFLOW_CLIP
        || overflow == Q_OVERFLOW_SCROLL
        || overflow == Q_OVERFLOW_AUTO;
}

static int rect_intersect(const SDL_Rect *a, const SDL_Rect *b, SDL_Rect *out)
{
    int x0;
    int y0;
    int x1;
    int y1;

    if (a == NULL || b == NULL || out == NULL) {
        return 0;
    }

    x0 = (a->x > b->x) ? a->x : b->x;
    y0 = (a->y > b->y) ? a->y : b->y;
    x1 = ((a->x + a->w) < (b->x + b->w)) ? (a->x + a->w) : (b->x + b->w);
    y1 = ((a->y + a->h) < (b->y + b->h)) ? (a->y + a->h) : (b->y + b->h);

    if (x1 <= x0 || y1 <= y0) {
        return 0;
    }

    out->x = x0;
    out->y = y0;
    out->w = x1 - x0;
    out->h = y1 - y0;
    return 1;
}

static int rect_intersects_margin(const SDL_Rect *rect, int vp_w, int vp_h, int margin)
{
    SDL_Rect vp;
    SDL_Rect isect;

    if (rect == NULL) {
        return 0;
    }

    vp.x = -margin;
    vp.y = -margin;
    vp.w = vp_w + margin * 2;
    vp.h = vp_h + margin * 2;
    return rect_intersect(rect, &vp, &isect);
}

static void sdl2_cache_clear(q_sdl2_win_t *win)
{
    q_sdl2_tex_entry_t *it;
    q_sdl2_tex_entry_t *next;

    if (win == NULL) {
        return;
    }

    it = win->cache;
    while (it != NULL) {
        next = it->next;
        SDL_DestroyTexture(it->texture);
        free(it);
        it = next;
    }
    win->cache = NULL;
}

static q_sdl2_tex_entry_t *sdl2_cache_find(q_sdl2_win_t *win, const q_box_t *box)
{
    q_sdl2_tex_entry_t *it;

    if (win == NULL || box == NULL) {
        return NULL;
    }

    for (it = win->cache; it != NULL; it = it->next) {
        if (it->box == box) {
            return it;
        }
    }

    return NULL;
}

static q_sdl2_tex_entry_t *sdl2_cache_ensure(q_sdl2_win_t *win,
                                             const q_box_t *box,
                                             int w,
                                             int h)
{
    q_sdl2_tex_entry_t *entry;

    entry = sdl2_cache_find(win, box);
    if (entry == NULL) {
        entry = (q_sdl2_tex_entry_t *) calloc(1, sizeof(*entry));
        if (entry == NULL) {
            return NULL;
        }
        entry->box = box;
        entry->next = win->cache;
        win->cache = entry;
    }

    if (entry->texture == NULL || entry->w != w || entry->h != h) {
        SDL_DestroyTexture(entry->texture);
        entry->texture = SDL_CreateTexture(win->renderer,
                                           SDL_PIXELFORMAT_RGBA32,
                                           SDL_TEXTUREACCESS_STATIC,
                                           w, h);
        if (entry->texture == NULL) {
            entry->w = 0;
            entry->h = 0;
            entry->uploaded_revision = 0;
            return NULL;
        }
        SDL_SetTextureBlendMode(entry->texture, SDL_BLENDMODE_BLEND);
        entry->w = w;
        entry->h = h;
        entry->uploaded_revision = 0;
    }

    entry->last_used_frame = win->frame_id;
    return entry;
}

static void sdl2_cache_prune(q_sdl2_win_t *win)
{
    q_sdl2_tex_entry_t *it;
    q_sdl2_tex_entry_t *prev;
    q_sdl2_tex_entry_t *next;

    if (win == NULL) {
        return;
    }

    prev = NULL;
    it = win->cache;
    while (it != NULL) {
        next = it->next;
        if ((uint64_t) (win->frame_id - it->last_used_frame) > Q_SDL2_CACHE_TTL_FRAMES)
        {
            if (prev != NULL) {
                prev->next = next;
            } else {
                win->cache = next;
            }
            SDL_DestroyTexture(it->texture);
            free(it);
        } else {
            prev = it;
        }
        it = next;
    }
}

static void sdl2_render_box_recursive(q_sdl2_win_t *win,
                                      quanton_view_t *view,
                                      q_box_t *box,
                                      float ancestor_scroll_x,
                                      float ancestor_scroll_y,
                                      const SDL_Rect *clip)
{
    SDL_Rect box_rect;
    SDL_Rect local_clip;
    SDL_Rect child_clip;
    SDL_Rect content_rect;
    q_box_t *child;
    float child_ancestor_scroll_x;
    float child_ancestor_scroll_y;

    if (win == NULL || view == NULL || box == NULL) {
        return;
    }

    box_rect.x = (int) lroundf(box->x - view->scroll_x - ancestor_scroll_x);
    box_rect.y = (int) lroundf(box->y - view->scroll_y - ancestor_scroll_y);
    box_rect.w = box->self_tile_w;
    box_rect.h = box->self_tile_h;

    if (clip != NULL) {
        local_clip = *clip;
    } else {
        local_clip.x = 0;
        local_clip.y = 0;
        local_clip.w = view->vp_width;
        local_clip.h = view->vp_height;
    }
    if (box_rect.w > 0 && box_rect.h > 0
        && box->self_tile != NULL
        && rect_intersects_margin(&box_rect, view->vp_width, view->vp_height,
                                  Q_SDL2_CACHE_MARGIN_PX))
    {
        q_sdl2_tex_entry_t *entry = sdl2_cache_ensure(win, box, box->self_tile_w, box->self_tile_h);
        if (entry != NULL) {
            if (entry->uploaded_revision != box->self_tile_revision) {
                SDL_UpdateTexture(entry->texture, NULL, box->self_tile, box->self_tile_w * 4);
                entry->uploaded_revision = box->self_tile_revision;
            }
            if (clip != NULL) {
                SDL_RenderSetClipRect(win->renderer, clip);
            } else {
                SDL_RenderSetClipRect(win->renderer, NULL);
            }
            SDL_RenderCopy(win->renderer, entry->texture, NULL, &box_rect);
        }
    }

    child_clip = local_clip;
    if (q_box_overflow_clips(box->overflow_x) || q_box_overflow_clips(box->overflow_y)) {
        int bleft = (int) ceilf(box->border_width[3]);
        int bright = (int) ceilf(box->border_width[1]);
        int btop = (int) ceilf(box->border_width[0]);
        int bbottom = (int) ceilf(box->border_width[2]);
        content_rect.x = box_rect.x + bleft;
        content_rect.y = box_rect.y + btop;
        content_rect.w = box_rect.w - bleft - bright;
        content_rect.h = box_rect.h - btop - bbottom;
        if (content_rect.w < 0) {
            content_rect.w = 0;
        }
        if (content_rect.h < 0) {
            content_rect.h = 0;
        }
        if (clip == NULL) {
            child_clip = content_rect;
        } else if (!rect_intersect(clip, &content_rect, &child_clip)) {
            return;
        }
    }

    child_ancestor_scroll_x = ancestor_scroll_x + (q_box_scrolls_x(box) ? box->scroll_x : 0.0f);
    child_ancestor_scroll_y = ancestor_scroll_y + (q_box_scrolls_y(box) ? box->scroll_y : 0.0f);

    for (child = box->first_child; child != NULL; child = child->next_sibling) {
        sdl2_render_box_recursive(win, view, child,
                                  child_ancestor_scroll_x,
                                  child_ancestor_scroll_y,
                                  &child_clip);
    }
}

static int sdl2_create_window(quanton_view_t *view, int w, int h, const char *title)
{
    q_sdl2_win_t *win;

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "quanton/sdl2: SDL_Init: %s\n", SDL_GetError());
        return -1;
    }

    win = (q_sdl2_win_t *) calloc(1, sizeof(*win));
    if (win == NULL) {
        SDL_Quit();
        return -1;
    }

    win->window = SDL_CreateWindow(
        (title != NULL) ? title : "quanton",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        w, h,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (win->window == NULL) {
        fprintf(stderr, "quanton/sdl2: SDL_CreateWindow: %s\n", SDL_GetError());
        free(win);
        SDL_Quit();
        return -1;
    }

    win->renderer = SDL_CreateRenderer(win->window, -1,
                                       SDL_RENDERER_ACCELERATED |
                                       SDL_RENDERER_PRESENTVSYNC);
    if (win->renderer == NULL) {
        win->renderer = SDL_CreateRenderer(win->window, -1, SDL_RENDERER_SOFTWARE);
    }
    if (win->renderer == NULL) {
        fprintf(stderr, "quanton/sdl2: SDL_CreateRenderer: %s\n", SDL_GetError());
        SDL_DestroyWindow(win->window);
        free(win);
        SDL_Quit();
        return -1;
    }

    win->texture = SDL_CreateTexture(win->renderer,
                                     SDL_PIXELFORMAT_RGBA32,
                                     SDL_TEXTUREACCESS_STREAMING,
                                     w, h);
    if (win->texture == NULL) {
        fprintf(stderr, "quanton/sdl2: SDL_CreateTexture: %s\n", SDL_GetError());
        SDL_DestroyRenderer(win->renderer);
        SDL_DestroyWindow(win->window);
        free(win);
        SDL_Quit();
        return -1;
    }

    win->tex_w = w;
    win->tex_h = h;

    view->window_handle = win;
    view->vp_width = w;
    view->vp_height = h;
    view->framebuffer = (uint8_t *) calloc((size_t) w * (size_t) h * 4u, 1u);
    if (view->framebuffer == NULL) {
        SDL_DestroyTexture(win->texture);
        SDL_DestroyRenderer(win->renderer);
        SDL_DestroyWindow(win->window);
        free(win);
        SDL_Quit();
        return -1;
    }

    return 0;
}

static void sdl2_render_view(quanton_view_t *view)
{
    q_sdl2_win_t *win;
    SDL_Rect viewport;

    if (view == NULL || view->window_handle == NULL) {
        return;
    }

    win = (q_sdl2_win_t *) view->window_handle;
    win->frame_id++;
    if (win->last_root != view->layout_root) {
        sdl2_cache_clear(win);
        win->last_root = view->layout_root;
    }

    SDL_SetRenderDrawColor(win->renderer, 255, 255, 255, 255);
    SDL_RenderClear(win->renderer);

    if (view->layout_root != NULL) {
        viewport.x = 0;
        viewport.y = 0;
        viewport.w = view->vp_width;
        viewport.h = view->vp_height;
        sdl2_render_box_recursive(win, view, view->layout_root, 0.0f, 0.0f, &viewport);
    }

    SDL_RenderSetClipRect(win->renderer, NULL);
    SDL_RenderPresent(win->renderer);
    sdl2_cache_prune(win);
}

static void sdl2_blit(quanton_view_t *view)
{
    q_sdl2_win_t *win;

    if (view == NULL || view->window_handle == NULL || view->framebuffer == NULL) {
        return;
    }

    win = (q_sdl2_win_t *) view->window_handle;

    if (win->tex_w != view->vp_width || win->tex_h != view->vp_height) {
        SDL_DestroyTexture(win->texture);
        win->texture = SDL_CreateTexture(win->renderer,
                                         SDL_PIXELFORMAT_RGBA32,
                                         SDL_TEXTUREACCESS_STREAMING,
                                         view->vp_width, view->vp_height);
        if (win->texture == NULL) {
            return;
        }
        win->tex_w = view->vp_width;
        win->tex_h = view->vp_height;
    }

    SDL_UpdateTexture(win->texture, NULL, view->framebuffer, view->vp_width * 4);
    SDL_RenderClear(win->renderer);
    SDL_RenderCopy(win->renderer, win->texture, NULL, NULL);
    SDL_RenderPresent(win->renderer);
}

static void sdl2_poll_events(quanton_view_t *view)
{
    SDL_Event sev;
    q_event_t ev;
    int need_render = 0;

    if (view == NULL || view->window_handle == NULL) {
        return;
    }

    while (SDL_PollEvent(&sev)) {
        memset(&ev, 0, sizeof(ev));

        switch (sev.type) {
        case SDL_QUIT:
            ev.type = Q_EVENT_CLOSE;
            view->should_close = 1;
            sdl2_dispatch(view, &ev);
            break;

        case SDL_WINDOWEVENT:
            switch (sev.window.event) {
            case SDL_WINDOWEVENT_EXPOSED:
                need_render = 1;
                break;

            case SDL_WINDOWEVENT_RESIZED:
            case SDL_WINDOWEVENT_SIZE_CHANGED: {
                int nw = sev.window.data1;
                int nh = sev.window.data2;
                uint8_t *new_fb;

                if (nw != view->vp_width || nh != view->vp_height) {
                    q_sdl2_win_t *win = (q_sdl2_win_t *) view->window_handle;

                    sdl2_cache_clear(win);
                    new_fb = (uint8_t *) realloc(view->framebuffer,
                                                 (size_t) nw * (size_t) nh * 4u);
                    if (new_fb != NULL) {
                        view->framebuffer = new_fb;
                        view->vp_width = nw;
                        view->vp_height = nh;

                        ev.type = Q_EVENT_RESIZE;
                        ev.new_width = nw;
                        ev.new_height = nh;
                        sdl2_dispatch(view, &ev);
                    }
                }
                break;
            }
            default:
                break;
            }
            break;

        case SDL_MOUSEMOTION:
            ev.type = Q_EVENT_MOUSE_MOVE;
            ev.mouse_x = sev.motion.x;
            ev.mouse_y = sev.motion.y;
            ev.key_mod = sdl2_mod((SDL_Keymod) SDL_GetModState());
            sdl2_dispatch(view, &ev);
            break;

        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP:
            ev.type = (sev.type == SDL_MOUSEBUTTONDOWN)
                      ? Q_EVENT_MOUSE_DOWN : Q_EVENT_MOUSE_UP;
            ev.mouse_x = sev.button.x;
            ev.mouse_y = sev.button.y;
            ev.mouse_button = (sev.button.button == SDL_BUTTON_LEFT)   ? 0 :
                              (sev.button.button == SDL_BUTTON_MIDDLE) ? 1 : 2;
            ev.key_mod = sdl2_mod((SDL_Keymod) SDL_GetModState());
            sdl2_dispatch(view, &ev);
            if (sev.type == SDL_MOUSEBUTTONUP) {
                q_event_t click_ev = ev;
                click_ev.type = Q_EVENT_MOUSE_CLICK;
                sdl2_dispatch(view, &click_ev);
            }
            break;

        case SDL_MOUSEWHEEL: {
            int mx;
            int my;
            int delta = sev.wheel.y;

            if (sev.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) {
                delta = -delta;
            }

            SDL_GetMouseState(&mx, &my);
            ev.type = Q_EVENT_MOUSE_WHEEL;
            ev.mouse_x = mx;
            ev.mouse_y = my;
            ev.wheel_delta = delta;
            ev.key_mod = sdl2_mod((SDL_Keymod) SDL_GetModState());
            sdl2_dispatch(view, &ev);
            break;
        }

        case SDL_KEYDOWN:
        case SDL_KEYUP:
            ev.type = (sev.type == SDL_KEYDOWN) ? Q_EVENT_KEY_DOWN : Q_EVENT_KEY_UP;
            ev.key_sym = (uint32_t) sev.key.keysym.sym;
            ev.key_mod = sdl2_mod((SDL_Keymod) sev.key.keysym.mod);
            sdl2_dispatch(view, &ev);
            break;

        default:
            break;
        }
    }

    if (need_render) {
        if (view->ctx != NULL && view->ctx->backend != NULL
            && view->ctx->backend->render_view != NULL)
        {
            view->ctx->backend->render_view(view);
        } else {
            sdl2_blit(view);
        }
    }
}

static void sdl2_destroy_window(quanton_view_t *view)
{
    q_sdl2_win_t *win;

    if (view == NULL || view->window_handle == NULL) {
        return;
    }

    win = (q_sdl2_win_t *) view->window_handle;

    sdl2_cache_clear(win);
    SDL_DestroyTexture(win->texture);
    SDL_DestroyRenderer(win->renderer);
    SDL_DestroyWindow(win->window);
    free(win);
    view->window_handle = NULL;

    free(view->framebuffer);
    view->framebuffer = NULL;

    SDL_Quit();
}

static void sdl2_set_title(quanton_view_t *view, const char *title)
{
    q_sdl2_win_t *win;

    if (view == NULL || view->window_handle == NULL) {
        return;
    }
    win = (q_sdl2_win_t *) view->window_handle;
    SDL_SetWindowTitle(win->window,
                       (title != NULL && title[0] != '\0') ? title : "quanton");
}

const q_backend_vt_t q_backend_sdl2 = {
    sdl2_create_window,
    sdl2_render_view,
    sdl2_blit,
    sdl2_poll_events,
    sdl2_destroy_window,
    sdl2_set_title,
};
