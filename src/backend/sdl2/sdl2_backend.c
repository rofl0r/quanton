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
#define Q_SDL2_CACHE_MAX_BYTES (100u * 1024u * 1024u)
#define Q_SCROLLBAR_THICKNESS 14

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

/*
 * Translate an SDL_Keycode into the backend-agnostic Q_KEY_* codes (or pass
 * through printable ASCII unchanged) expected by q_event_dispatch() for text
 * input navigation/editing.
 */
static uint32_t sdl2_translate_key(SDL_Keycode sym)
{
    switch (sym) {
    case SDLK_LEFT:      return Q_KEY_LEFT;
    case SDLK_RIGHT:     return Q_KEY_RIGHT;
    case SDLK_UP:        return Q_KEY_UP;
    case SDLK_DOWN:      return Q_KEY_DOWN;
    case SDLK_PAGEUP:    return Q_KEY_PAGEUP;
    case SDLK_PAGEDOWN:  return Q_KEY_PAGEDOWN;
    case SDLK_HOME:      return Q_KEY_HOME;
    case SDLK_END:       return Q_KEY_END;
    case SDLK_BACKSPACE: return Q_KEY_BACKSPACE;
    case SDLK_DELETE:    return Q_KEY_DELETE;
    case SDLK_RETURN:
    case SDLK_KP_ENTER:  return Q_KEY_ENTER;
    default:             return (uint32_t) sym;
    }
}

static int sdl2_key_is_repeat_coalescible(uint32_t key_sym)
{
    if (key_sym == Q_KEY_LEFT || key_sym == Q_KEY_RIGHT
        || key_sym == Q_KEY_UP || key_sym == Q_KEY_DOWN
        || key_sym == Q_KEY_HOME || key_sym == Q_KEY_END
        || key_sym == Q_KEY_PAGEUP || key_sym == Q_KEY_PAGEDOWN
        || key_sym == Q_KEY_BACKSPACE || key_sym == Q_KEY_DELETE
        || key_sym == Q_KEY_ENTER)
    {
        return 1;
    }
    return key_sym >= 0x20u && key_sym <= 0x7Eu;
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

static void q_box_content_extent(const q_box_t *box, float *out_w, float *out_h)
{
    q_box_t *child;
    float max_w = 0.0f;
    float max_h = 0.0f;

    if (box == NULL || out_w == NULL || out_h == NULL) {
        return;
    }

    for (child = box->first_child; child != NULL; child = child->next_sibling) {
        float local_right = (child->x - box->x) + child->width;
        float local_bottom = (child->y - box->y) + child->height;
        if (local_right > max_w) {
            max_w = local_right;
        }
        if (local_bottom > max_h) {
            max_h = local_bottom;
        }
    }

    *out_w = (max_w > 0.0f) ? max_w : 0.0f;
    *out_h = (max_h > 0.0f) ? max_h : 0.0f;
}

static int q_box_has_vertical_scrollbar(const q_box_t *box, float content_h, float viewport_h)
{
    if (box == NULL) {
        return 0;
    }
    if (box->overflow_y == Q_OVERFLOW_SCROLL) {
        return 1;
    }
    return box->overflow_y == Q_OVERFLOW_AUTO && content_h > viewport_h;
}

static int q_box_has_horizontal_scrollbar(const q_box_t *box, float content_w, float viewport_w)
{
    if (box == NULL) {
        return 0;
    }
    if (box->overflow_x == Q_OVERFLOW_SCROLL) {
        return 1;
    }
    return box->overflow_x == Q_OVERFLOW_AUTO && content_w > viewport_w;
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

static void sdl2_cache_prune(q_sdl2_win_t *win, const quanton_view_t *view)
{
    q_sdl2_tex_entry_t *oldest;
    q_sdl2_tex_entry_t *oldest_prev;
    q_sdl2_tex_entry_t *it;
    q_sdl2_tex_entry_t *prev;
    size_t total_bytes = 0;
    size_t tex_bytes;

    if (win == NULL) {
        return;
    }

    for (it = win->cache; it != NULL; it = it->next) {
        if (it->w > 0 && it->h > 0) {
            total_bytes += (size_t) it->w * (size_t) it->h * 4u;
        }
    }

    size_t limit = Q_SDL2_CACHE_MAX_BYTES;
    if (view != NULL && view->texture_cache_limit_bytes > 0u) {
        limit = view->texture_cache_limit_bytes;
    }

    while (total_bytes > limit) {
        oldest = NULL;
        oldest_prev = NULL;
        prev = NULL;
        for (it = win->cache; it != NULL; it = it->next) {
            if (oldest == NULL || it->last_used_frame < oldest->last_used_frame) {
                oldest = it;
                oldest_prev = prev;
            }
            prev = it;
        }

        if (oldest == NULL) {
            break;
        }

        tex_bytes = 0;
        if (oldest->w > 0 && oldest->h > 0) {
            tex_bytes = (size_t) oldest->w * (size_t) oldest->h * 4u;
        }
        if (oldest_prev != NULL) {
            oldest_prev->next = oldest->next;
        } else {
            win->cache = oldest->next;
        }
        SDL_DestroyTexture(oldest->texture);
        free(oldest);
        if (tex_bytes > total_bytes) {
            total_bytes = 0;
        } else {
            total_bytes -= tex_bytes;
        }
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
        float content_w = 0.0f;
        float content_h = 0.0f;
        int show_vertical;
        int show_horizontal;
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
        q_box_content_extent(box, &content_w, &content_h);
        show_vertical = q_box_has_vertical_scrollbar(box, content_h, (float) content_rect.h);
        show_horizontal = q_box_has_horizontal_scrollbar(box, content_w, (float) content_rect.w);
        if (box->parent != NULL) {
            if (show_vertical) {
                content_rect.w -= Q_SCROLLBAR_THICKNESS;
                if (content_rect.w < 0) {
                    content_rect.w = 0;
                }
            }
            if (show_horizontal) {
                content_rect.h -= Q_SCROLLBAR_THICKNESS;
                if (content_rect.h < 0) {
                    content_rect.h = 0;
                }
            }
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

    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "opengl");

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
    sdl2_cache_prune(win, view);
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

static void sdl2_flush_wheel(quanton_view_t *view, int *pending, int *delta_accum,
                             int mx, int my, uint32_t mod)
{
    q_event_t ev;

    if (!*pending) {
        return;
    }

    memset(&ev, 0, sizeof(ev));
    ev.type = Q_EVENT_MOUSE_WHEEL;
    ev.mouse_x = mx;
    ev.mouse_y = my;
    ev.wheel_delta = *delta_accum;
    ev.key_mod = mod;
    sdl2_dispatch(view, &ev);

    *pending = 0;
    *delta_accum = 0;
}

static void sdl2_poll_events(quanton_view_t *view)
{
    SDL_Event sev;
    q_event_t ev;
    int need_render = 0;
    int wheel_pending = 0;
    int wheel_delta_accum = 0;
    int wheel_mx = 0;
    int wheel_my = 0;
    uint32_t wheel_mod = 0;
    int motion_pending = 0;
    int motion_mx = 0;
    int motion_my = 0;
    uint32_t motion_mod = 0;
    int key_pending = 0;
    uint32_t key_sym = 0;
    uint32_t key_mod = 0;
    int key_repeat = 0;

    if (view == NULL || view->window_handle == NULL) {
        return;
    }
    view->defer_updates = 1;

#define SDL2_FLUSH_MOTION() do { \
    if (motion_pending) { \
        memset(&ev, 0, sizeof(ev)); \
        ev.type = Q_EVENT_MOUSE_MOVE; \
        ev.mouse_x = motion_mx; \
        ev.mouse_y = motion_my; \
        ev.key_mod = motion_mod; \
        sdl2_dispatch(view, &ev); \
        motion_pending = 0; \
    } \
} while (0)
#define SDL2_FLUSH_KEYDOWN() do { \
    if (key_pending) { \
        memset(&ev, 0, sizeof(ev)); \
        ev.type = Q_EVENT_KEY_DOWN; \
        ev.key_sym = key_sym; \
        ev.key_mod = key_mod; \
        ev.key_repeat = (key_repeat > 0) ? key_repeat : 1; \
        sdl2_dispatch(view, &ev); \
        key_pending = 0; \
        key_repeat = 0; \
    } \
} while (0)

    while (SDL_PollEvent(&sev)) {
        /*
         * SDL2 can fire thousands of SDL_MOUSEWHEEL events per real scroll
         * gesture. Dispatching (and therefore re-rendering) on every single
         * one is what makes wheel scrolling feel laggy. Instead, accumulate
         * the scroll distance across consecutive wheel events and only act
         * on it once we drain the queue or hit a different event type, so a
         * whole burst of wheel events results in a single scroll + redraw.
         */
        if (sev.type != SDL_MOUSEWHEEL) {
            sdl2_flush_wheel(view, &wheel_pending, &wheel_delta_accum,
                             wheel_mx, wheel_my, wheel_mod);
        }
        if (sev.type != SDL_MOUSEMOTION) {
            SDL2_FLUSH_MOTION();
        }
        if (sev.type != SDL_KEYDOWN) {
            SDL2_FLUSH_KEYDOWN();
        }

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
            motion_mx = sev.motion.x;
            motion_my = sev.motion.y;
            motion_mod = sdl2_mod((SDL_Keymod) SDL_GetModState());
            motion_pending = 1;
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
            wheel_delta_accum += delta;
            wheel_mx = mx;
            wheel_my = my;
            wheel_mod = sdl2_mod((SDL_Keymod) SDL_GetModState());
            wheel_pending = 1;
            break;
        }

        case SDL_KEYDOWN:
            {
                uint32_t sym = sdl2_translate_key(sev.key.keysym.sym);
                uint32_t mod = sdl2_mod((SDL_Keymod) sev.key.keysym.mod);
                if (sdl2_key_is_repeat_coalescible(sym)) {
                    if (key_pending && key_sym == sym && key_mod == mod) {
                        key_repeat++;
                    } else {
                        SDL2_FLUSH_KEYDOWN();
                        key_pending = 1;
                        key_sym = sym;
                        key_mod = mod;
                        key_repeat = 1;
                    }
                } else {
                    SDL2_FLUSH_KEYDOWN();
                    ev.type = Q_EVENT_KEY_DOWN;
                    ev.key_sym = sym;
                    ev.key_mod = mod;
                    ev.key_repeat = 1;
                    sdl2_dispatch(view, &ev);
                }
            }
            break;

        case SDL_KEYUP:
            SDL2_FLUSH_KEYDOWN();
            ev.type = Q_EVENT_KEY_UP;
            ev.key_sym = sdl2_translate_key(sev.key.keysym.sym);
            ev.key_mod = sdl2_mod((SDL_Keymod) sev.key.keysym.mod);
            ev.key_repeat = 1;
            sdl2_dispatch(view, &ev);
            break;

        default:
            break;
        }
    }

    sdl2_flush_wheel(view, &wheel_pending, &wheel_delta_accum,
                     wheel_mx, wheel_my, wheel_mod);
    SDL2_FLUSH_MOTION();
    SDL2_FLUSH_KEYDOWN();
#undef SDL2_FLUSH_MOTION
#undef SDL2_FLUSH_KEYDOWN
    view->defer_updates = 0;
    if (view->dirty_flags != 0u) {
        q_view_update(view);
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
