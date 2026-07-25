/*
 * X11 backend for quanton.
 *
 * Renders by blitting a pre-composed RGBA framebuffer via XPutImage
 * after converting to the native X11 BGRX pixel layout.
 *
 * Event loop handles:
 *   - WM_DELETE_WINDOW (close button / Alt-F4)  → Q_EVENT_CLOSE
 *   - Expose                                     → re-blit
 *   - ConfigureNotify (resize)                   → Q_EVENT_RESIZE
 *   - ButtonPress / ButtonRelease                → Q_EVENT_MOUSE_DOWN/UP
 *   - Button4 / Button5 (scroll wheel)           → Q_EVENT_MOUSE_WHEEL
 *   - MotionNotify                               → Q_EVENT_MOUSE_MOVE
 *   - KeyPress / KeyRelease                      → Q_EVENT_KEY_DOWN/UP
 */

#include "quanton.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Internal per-window state ─────────────────────────────────────────── */

typedef struct {
    Display *display;
    Window   window;
    GC       gc;
    Atom     wm_delete;
    uint8_t *xbuf;       /* BGRX conversion buffer, vp_width × vp_height × 4 */
    int      buf_w;
    int      buf_h;
} q_x11_win_t;

/* ── Pixel format conversion ────────────────────────────────────────────── */

/*
 * Convert RGBA8 framebuffer to BGRX (X11 native 32-bit little-endian layout).
 * The alpha byte is zeroed because X11 24/32-bit visuals treat it as padding.
 */
static void rgba_to_bgrx(const uint8_t *src, uint8_t *dst, int npixels)
{
    int i;
    for (i = 0; i < npixels; i++) {
        dst[i * 4 + 0] = src[i * 4 + 2]; /* B */
        dst[i * 4 + 1] = src[i * 4 + 1]; /* G */
        dst[i * 4 + 2] = src[i * 4 + 0]; /* R */
        dst[i * 4 + 3] = 0;              /* pad / alpha ignored by X11 */
    }
}

/* ── Helper: dispatch an event to the application callback ─────────────── */

static void x11_dispatch(quanton_view_t *view, q_event_t *ev)
{
    q_event_dispatch(view, ev);
}

/* ── Backend vtable functions ───────────────────────────────────────────── */

static int x11_create_window(quanton_view_t *view, int w, int h, const char *title)
{
    q_x11_win_t  *win;
    Display      *dpy;
    int           screen;
    unsigned long black;

    win = (q_x11_win_t *) calloc(1, sizeof(*win));
    if (win == NULL) {
        return -1;
    }

    dpy = XOpenDisplay(NULL);
    if (dpy == NULL) {
        fprintf(stderr, "quanton/x11: cannot open display\n");
        free(win);
        return -1;
    }

    screen = DefaultScreen(dpy);
    black  = BlackPixel(dpy, screen);

    win->window = XCreateSimpleWindow(dpy, RootWindow(dpy, screen),
                                      0, 0,
                                      (unsigned int) w, (unsigned int) h,
                                      0, black, black);
    if (win->window == None) {
        fprintf(stderr, "quanton/x11: XCreateSimpleWindow failed\n");
        XCloseDisplay(dpy);
        free(win);
        return -1;
    }

    /* Window title */
    XStoreName(dpy, win->window, (title != NULL) ? title : "quanton");

    /* Select the event types we care about */
    XSelectInput(dpy, win->window,
                 ExposureMask |
                 KeyPressMask | KeyReleaseMask |
                 ButtonPressMask | ButtonReleaseMask |
                 PointerMotionMask |
                 StructureNotifyMask);

    /*
     * Register WM_DELETE_WINDOW so that clicking the close button (or pressing
     * Alt-F4) delivers a ClientMessage rather than instantly killing the client.
     */
    win->wm_delete = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(dpy, win->window, &win->wm_delete, 1);

    win->gc = XCreateGC(dpy, win->window, 0, NULL);
    if (win->gc == 0) {
        XDestroyWindow(dpy, win->window);
        XCloseDisplay(dpy);
        free(win);
        return -1;
    }

    /* Allocate the BGRX pixel conversion buffer */
    win->xbuf  = (uint8_t *) malloc((size_t) w * (size_t) h * 4u);
    win->buf_w = w;
    win->buf_h = h;
    if (win->xbuf == NULL) {
        XFreeGC(dpy, win->gc);
        XDestroyWindow(dpy, win->window);
        XCloseDisplay(dpy);
        free(win);
        return -1;
    }

    win->display = dpy;

    XMapWindow(dpy, win->window);
    XFlush(dpy);

    view->window_handle = win;
    view->vp_width      = w;
    view->vp_height     = h;

    /* Allocate the RGBA framebuffer */
    view->framebuffer = (uint8_t *) calloc((size_t) w * (size_t) h * 4u, 1u);
    if (view->framebuffer == NULL) {
        free(win->xbuf);
        XFreeGC(dpy, win->gc);
        XDestroyWindow(dpy, win->window);
        XCloseDisplay(dpy);
        free(win);
        view->window_handle = NULL;
        return -1;
    }

    return 0;
}

static void x11_blit(quanton_view_t *view)
{
    q_x11_win_t *win;
    XImage      *img;
    int          depth;
    Visual      *visual;
    int          screen;

    if (view == NULL || view->window_handle == NULL || view->framebuffer == NULL) {
        return;
    }

    win    = (q_x11_win_t *) view->window_handle;
    screen = DefaultScreen(win->display);
    depth  = DefaultDepth(win->display, screen);
    visual = DefaultVisual(win->display, screen);

    /* Grow the conversion buffer if the viewport was resized */
    if (win->buf_w != view->vp_width || win->buf_h != view->vp_height) {
        uint8_t *nb = (uint8_t *) realloc(win->xbuf,
                                           (size_t) view->vp_width *
                                           (size_t) view->vp_height * 4u);
        if (nb == NULL) {
            return;
        }
        win->xbuf  = nb;
        win->buf_w = view->vp_width;
        win->buf_h = view->vp_height;
    }

    rgba_to_bgrx(view->framebuffer, win->xbuf,
                 view->vp_width * view->vp_height);

    /*
     * XCreateImage wraps win->xbuf without copying it.
     * We set img->data = NULL before XDestroyImage to prevent it from
     * freeing our buffer.
     */
    img = XCreateImage(win->display, visual, (unsigned int) depth,
                       ZPixmap, 0,
                       (char *) win->xbuf,
                       (unsigned int) view->vp_width,
                       (unsigned int) view->vp_height,
                       32,
                       view->vp_width * 4);
    if (img == NULL) {
        return;
    }

    XPutImage(win->display, win->window, win->gc, img,
              0, 0, 0, 0,
              (unsigned int) view->vp_width,
              (unsigned int) view->vp_height);

    img->data = NULL; /* do not free win->xbuf */
    XDestroyImage(img);

    XFlush(win->display);
}

static void x11_poll_events(quanton_view_t *view)
{
    q_x11_win_t *win;
    XEvent       xev;
    q_event_t    ev;
    int          need_blit = 0;

    if (view == NULL || view->window_handle == NULL) {
        return;
    }

    win = (q_x11_win_t *) view->window_handle;

    while (XPending(win->display) > 0) {
        XNextEvent(win->display, &xev);
        memset(&ev, 0, sizeof(ev));

        switch (xev.type) {

        case Expose:
            /*
             * count == 0 means this is the last Expose in a sequence;
             * batch up multiple expose events and blit once at the end.
             */
            if (xev.xexpose.count == 0) {
                need_blit = 1;
            }
            break;

        case ConfigureNotify:
            if (xev.xconfigure.width  != view->vp_width ||
                xev.xconfigure.height != view->vp_height)
            {
                int      nw    = xev.xconfigure.width;
                int      nh    = xev.xconfigure.height;
                uint8_t *new_fb;

                new_fb = (uint8_t *) realloc(view->framebuffer,
                                              (size_t) nw * (size_t) nh * 4u);
                if (new_fb != NULL) {
                    view->framebuffer = new_fb;
                    view->vp_width    = nw;
                    view->vp_height   = nh;

                    ev.type       = Q_EVENT_RESIZE;
                    ev.new_width  = nw;
                    ev.new_height = nh;
                    x11_dispatch(view, &ev);
                }
            }
            break;

        case ClientMessage:
            /*
             * WM_DELETE_WINDOW: the user clicked the window's close button
             * or pressed Alt-F4.  Set should_close and dispatch Q_EVENT_CLOSE
             * so the application can decide how to respond (confirm-dialog,
             * immediate exit, etc.).
             */
            if ((Atom) xev.xclient.data.l[0] == win->wm_delete) {
                ev.type = Q_EVENT_CLOSE;
                view->should_close = 1;
                x11_dispatch(view, &ev);
            }
            break;

        case ButtonPress:
        case ButtonRelease: {
            unsigned int btn = xev.xbutton.button;
            uint32_t     mod;

            mod = ((xev.xbutton.state & ShiftMask)   ? 1u : 0u) |
                  ((xev.xbutton.state & ControlMask)  ? 2u : 0u) |
                  ((xev.xbutton.state & Mod1Mask)     ? 4u : 0u);

            if (btn == Button4 || btn == Button5) {
                /* Vertical scroll wheel */
                ev.type        = Q_EVENT_MOUSE_WHEEL;
                ev.mouse_x     = xev.xbutton.x;
                ev.mouse_y     = xev.xbutton.y;
                ev.wheel_delta = (btn == Button4) ? 1 : -1;
                ev.key_mod     = mod;
            } else {
                ev.type         = (xev.type == ButtonPress)
                                  ? Q_EVENT_MOUSE_DOWN : Q_EVENT_MOUSE_UP;
                ev.mouse_x      = xev.xbutton.x;
                ev.mouse_y      = xev.xbutton.y;
                ev.mouse_button = (btn == Button1) ? 0 :
                                  (btn == Button2) ? 1 : 2;
                ev.key_mod      = mod;
            }
            x11_dispatch(view, &ev);
            if (xev.type == ButtonRelease && btn != Button4 && btn != Button5) {
                q_event_t click_ev = ev;
                click_ev.type = Q_EVENT_MOUSE_CLICK;
                x11_dispatch(view, &click_ev);
            }
            break;
        }

        case MotionNotify:
            ev.type    = Q_EVENT_MOUSE_MOVE;
            ev.mouse_x = xev.xmotion.x;
            ev.mouse_y = xev.xmotion.y;
            ev.key_mod = ((xev.xmotion.state & ShiftMask)   ? 1u : 0u) |
                         ((xev.xmotion.state & ControlMask)  ? 2u : 0u) |
                         ((xev.xmotion.state & Mod1Mask)     ? 4u : 0u);
            x11_dispatch(view, &ev);
            break;

        case KeyPress:
        case KeyRelease: {
            KeySym ks = XLookupKeysym(&xev.xkey, 0);
            ev.type    = (xev.type == KeyPress) ? Q_EVENT_KEY_DOWN : Q_EVENT_KEY_UP;
            ev.key_sym = (uint32_t) ks;
            ev.key_mod = ((xev.xkey.state & ShiftMask)   ? 1u : 0u) |
                         ((xev.xkey.state & ControlMask)  ? 2u : 0u) |
                         ((xev.xkey.state & Mod1Mask)     ? 4u : 0u);
            x11_dispatch(view, &ev);
            break;
        }

        default:
            break;
        }
    }

    /* Flush pending redraws after all events are consumed */
    if (need_blit) {
        x11_blit(view);
    }
}

static void x11_destroy_window(quanton_view_t *view)
{
    q_x11_win_t *win;

    if (view == NULL || view->window_handle == NULL) {
        return;
    }

    win = (q_x11_win_t *) view->window_handle;

    free(win->xbuf);
    XFreeGC(win->display, win->gc);
    XDestroyWindow(win->display, win->window);
    XCloseDisplay(win->display);
    free(win);
    view->window_handle = NULL;

    free(view->framebuffer);
    view->framebuffer = NULL;
}

static void x11_set_title(quanton_view_t *view, const char *title)
{
    q_x11_win_t *win;

    if (view == NULL || view->window_handle == NULL) {
        return;
    }

    win = (q_x11_win_t *) view->window_handle;
    XStoreName(win->display, win->window,
               (title != NULL && title[0] != '\0') ? title : "quanton");
    XFlush(win->display);
}

/* ── Public vtable instance ─────────────────────────────────────────────── */

const q_backend_vt_t q_backend_x11 = {
    x11_create_window,
    NULL,
    x11_blit,
    x11_poll_events,
    x11_destroy_window,
    x11_set_title,
};
