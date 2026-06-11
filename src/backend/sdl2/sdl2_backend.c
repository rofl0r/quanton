/*
 * SDL2 backend for quanton.
 *
 * Renders by uploading the RGBA framebuffer as a streaming SDL_Texture and
 * compositing it onto the renderer each frame.
 *
 * Event loop handles:
 *   - SDL_QUIT                         → Q_EVENT_CLOSE
 *   - SDL_WINDOWEVENT_EXPOSED          → re-blit
 *   - SDL_WINDOWEVENT_RESIZED /
 *     SDL_WINDOWEVENT_SIZE_CHANGED     → Q_EVENT_RESIZE
 *   - SDL_MOUSEMOTION                  → Q_EVENT_MOUSE_MOVE
 *   - SDL_MOUSEBUTTONDOWN/UP           → Q_EVENT_MOUSE_DOWN/UP
 *   - SDL_MOUSEWHEEL                   → Q_EVENT_MOUSE_WHEEL
 *   - SDL_KEYDOWN/UP                   → Q_EVENT_KEY_DOWN/UP
 */

#include "quanton.h"

#include <SDL2/SDL.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Internal per-window state ─────────────────────────────────────────── */

typedef struct {
    SDL_Window   *window;
    SDL_Renderer *renderer;
    SDL_Texture  *texture;  /* SDL_TEXTUREACCESS_STREAMING, vp_width × vp_height */
    int           tex_w;
    int           tex_h;
} q_sdl2_win_t;

/* ── Helper: dispatch an event to the application callback ─────────────── */

static void sdl2_dispatch(quanton_view_t *view, q_event_t *ev)
{
    q_event_dispatch(view, ev);
}

/* ── Helper: build key modifier flags from SDL modifier mask ────────────── */

static uint32_t sdl2_mod(SDL_Keymod mod)
{
    return ((mod & KMOD_SHIFT) ? 1u : 0u) |
           ((mod & KMOD_CTRL)  ? 2u : 0u) |
           ((mod & KMOD_ALT)   ? 4u : 0u);
}

/* ── Backend vtable functions ───────────────────────────────────────────── */

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

    /* Prefer hardware-accelerated renderer; fall back to software */
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

    /*
     * SDL_PIXELFORMAT_RGBA32 is an endian-aware alias that always maps to
     * [R][G][B][A] in memory, matching our framebuffer layout.
     */
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
    view->vp_width      = w;
    view->vp_height     = h;

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

static void sdl2_blit(quanton_view_t *view)
{
    q_sdl2_win_t *win;

    if (view == NULL || view->window_handle == NULL || view->framebuffer == NULL) {
        return;
    }

    win = (q_sdl2_win_t *) view->window_handle;

    /*
     * Recreate the texture if the viewport dimensions changed (e.g. after a
     * resize event).  SDL textures have a fixed size; they cannot be resized
     * in-place.
     */
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

    /* Upload RGBA framebuffer to GPU texture */
    SDL_UpdateTexture(win->texture, NULL,
                      view->framebuffer, view->vp_width * 4);

    SDL_RenderClear(win->renderer);
    SDL_RenderCopy(win->renderer, win->texture, NULL, NULL);
    SDL_RenderPresent(win->renderer);
}

static void sdl2_poll_events(quanton_view_t *view)
{
    SDL_Event     sev;
    q_event_t     ev;
    int           need_blit = 0;

    if (view == NULL || view->window_handle == NULL) {
        return;
    }

    while (SDL_PollEvent(&sev)) {
        memset(&ev, 0, sizeof(ev));

        switch (sev.type) {

        case SDL_QUIT:
            /*
             * SDL_QUIT covers both the window's close button and any
             * platform-level quit signal (e.g. Alt-F4, Cmd-Q on macOS).
             */
            ev.type = Q_EVENT_CLOSE;
            view->should_close = 1;
            sdl2_dispatch(view, &ev);
            break;

        case SDL_WINDOWEVENT:
            switch (sev.window.event) {

            case SDL_WINDOWEVENT_EXPOSED:
                /*
                 * The window was uncovered (another window moved away, etc.).
                 * Re-blit after all events are processed.
                 */
                need_blit = 1;
                break;

            case SDL_WINDOWEVENT_RESIZED:
            case SDL_WINDOWEVENT_SIZE_CHANGED: {
                int      nw = sev.window.data1;
                int      nh = sev.window.data2;
                uint8_t *new_fb;

                if (nw != view->vp_width || nh != view->vp_height) {
                    new_fb = (uint8_t *) realloc(view->framebuffer,
                                                  (size_t) nw * (size_t) nh * 4u);
                    if (new_fb != NULL) {
                        view->framebuffer = new_fb;
                        view->vp_width    = nw;
                        view->vp_height   = nh;

                        ev.type       = Q_EVENT_RESIZE;
                        ev.new_width  = nw;
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
            ev.type    = Q_EVENT_MOUSE_MOVE;
            ev.mouse_x = sev.motion.x;
            ev.mouse_y = sev.motion.y;
            ev.key_mod = sdl2_mod((SDL_Keymod) SDL_GetModState());
            sdl2_dispatch(view, &ev);
            break;

        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP:
            ev.type         = (sev.type == SDL_MOUSEBUTTONDOWN)
                              ? Q_EVENT_MOUSE_DOWN : Q_EVENT_MOUSE_UP;
            ev.mouse_x      = sev.button.x;
            ev.mouse_y      = sev.button.y;
            ev.mouse_button = (sev.button.button == SDL_BUTTON_LEFT)   ? 0 :
                              (sev.button.button == SDL_BUTTON_MIDDLE) ? 1 : 2;
            ev.key_mod      = sdl2_mod((SDL_Keymod) SDL_GetModState());
            sdl2_dispatch(view, &ev);
            if (sev.type == SDL_MOUSEBUTTONUP) {
                q_event_t click_ev = ev;
                click_ev.type = Q_EVENT_MOUSE_CLICK;
                sdl2_dispatch(view, &click_ev);
            }
            break;

        case SDL_MOUSEWHEEL: {
            int mx, my;
            int delta = sev.wheel.y;

            /* SDL2 ≥ 2.0.4: respect natural/flipped scroll direction */
            if (sev.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) {
                delta = -delta;
            }

            SDL_GetMouseState(&mx, &my);
            ev.type        = Q_EVENT_MOUSE_WHEEL;
            ev.mouse_x     = mx;
            ev.mouse_y     = my;
            ev.wheel_delta = delta;
            ev.key_mod     = sdl2_mod((SDL_Keymod) SDL_GetModState());
            sdl2_dispatch(view, &ev);
            break;
        }

        case SDL_KEYDOWN:
        case SDL_KEYUP:
            ev.type    = (sev.type == SDL_KEYDOWN) ? Q_EVENT_KEY_DOWN : Q_EVENT_KEY_UP;
            ev.key_sym = (uint32_t) sev.key.keysym.sym;
            ev.key_mod = sdl2_mod((SDL_Keymod) sev.key.keysym.mod);
            sdl2_dispatch(view, &ev);
            break;

        default:
            break;
        }
    }

    /* Flush any pending expose redraws after all events are consumed */
    if (need_blit) {
        sdl2_blit(view);
    }
}

static void sdl2_destroy_window(quanton_view_t *view)
{
    q_sdl2_win_t *win;

    if (view == NULL || view->window_handle == NULL) {
        return;
    }

    win = (q_sdl2_win_t *) view->window_handle;

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

/* ── Public vtable instance ─────────────────────────────────────────────── */

const q_backend_vt_t q_backend_sdl2 = {
    sdl2_create_window,
    sdl2_blit,
    sdl2_poll_events,
    sdl2_destroy_window,
    sdl2_set_title,
};
