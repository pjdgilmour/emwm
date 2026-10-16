/* End-to-end title button tests. Run only on a private X server. */
#define _POSIX_C_SOURCE 200809L
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/extensions/Xrender.h>
#include <X11/extensions/Xcomposite.h>
#include <X11/extensions/XTest.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/wait.h>

/* _MOTIF_WM_HINTS wire values, without a dependency on Motif headers. */
enum { ALL = 1, RESIZE = 2, MOVE = 4, MINIMIZE = 8, MAXIMIZE = 16,
       CLOSE = 32, TITLE = 8, MENU = 16, MIN_BUTTON = 32, MAX_BUTTON = 64 };
static Display *d;
static Window root;
static Atom protocols, delete_window, hints, state;

static void settle(void)
{
    struct timespec delay = {0, 120000000};
    XSync(d, False);
    nanosleep(&delay, NULL);
    XSync(d, False);
}

static void set_hints(Window w, long functions, long decorations)
{
    long values[5] = {3, functions, decorations, 0, 0};
    XChangeProperty(d, w, hints, hints, 32, PropModeReplace,
                    (unsigned char *)values, 5);
    settle();
}

static Window create_window(long functions, long decorations, int width)
{
    Window w;
    if (getenv("ARGB_CLIENTS")) {
        XVisualInfo query = {0}, *visuals;
        int count, i;
        XSetWindowAttributes attrs = {0};
        query.screen = DefaultScreen(d);
        query.depth = 32;
        query.class = TrueColor;
        visuals = XGetVisualInfo(d, VisualScreenMask | VisualDepthMask | VisualClassMask,
                                &query, &count);
        for (i = 0; i < count; i++) {
            XRenderPictFormat *format = XRenderFindVisualFormat(d, visuals[i].visual);
            if (format && format->direct.alphaMask) break;
        }
        assert(i < count);
        attrs.colormap = XCreateColormap(d, root, visuals[i].visual, AllocNone);
        attrs.background_pixel = 0;
        attrs.border_pixel = 0;
        w = XCreateWindow(d, root, 120, 100, width, 180, 0, 32, InputOutput,
                          visuals[i].visual, CWColormap | CWBackPixel | CWBorderPixel,
                          &attrs);
        XFree(visuals);
    }
    else {
        w = XCreateSimpleWindow(d, root, 120, 100, width, 180, 0, 0, 0xb0b0b0);
    }
    XSizeHints sizes = {0};
    sizes.flags = USPosition | PMinSize;
    sizes.min_width = sizes.min_height = 1;
    XSetWMNormalHints(d, w, &sizes);
    XStoreName(d, w, "Motif close button regression test");
    XSelectInput(d, w, StructureNotifyMask | PropertyChangeMask);
    XSetWMProtocols(d, w, &delete_window, 1);
    set_hints(w, functions, decorations);
    XMapWindow(d, w);
    settle();
    return w;
}

static Window frame(Window w)
{
    Window parent, r, *children, current = w;
    unsigned int n;
    do {
        assert(XQueryTree(d, current, &r, &parent, &children, &n));
        if (children) XFree(children);
        if (parent == root) return current;
        current = parent;
    } while (current != None);
    abort();
}

/* Verify per-pixel alpha, opaque text/decoration, and actual picom output. */
static void check_argb(Window w)
{
    Window f = frame(w), child;
    XWindowAttributes fa, ca;
    XRenderPictFormat *format;
    Picture picture;
    Pixmap pixmap;
    XImage *pixels;
    int x, y, cx, cy;
    unsigned long pixel;
    XRenderColor half_red = {32768, 0, 0, 32768};
    XRenderColor white = {65535, 65535, 65535, 65535};
    XRenderColor clear = {0, 0, 0, 0};

    XGetWindowAttributes(d, f, &fa);
    XGetWindowAttributes(d, w, &ca);
    format = XRenderFindVisualFormat(d, ca.visual);
    picture = XRenderCreatePicture(d, w, format, 0, NULL);
    XRenderFillRectangle(d, PictOpSrc, picture, &half_red, 0, 0, ca.width, ca.height);
    XRenderFillRectangle(d, PictOpSrc, picture, &white, 20, 20, 20, 20);
    XRenderFillRectangle(d, PictOpSrc, picture, &clear, 50, 20, 20, 20);
    XRenderFreePicture(d, picture);
    settle();
    XTranslateCoordinates(d, w, f, 0, 0, &x, &y, &child);
    pixmap = XCompositeNameWindowPixmap(d, f);
    pixels = XGetImage(d, pixmap, 0, 0, fa.width, fa.height, AllPlanes, ZPixmap);
    assert(pixels);
    pixel = XGetPixel(pixels, x + 10, y + 10);
    printf("ARGB frame depth=%d, half-transparent pixel=%#lx\n", fa.depth, pixel);
    assert(fa.depth == 32);
    assert(((pixel >> 24) & 255) >= 127 && ((pixel >> 24) & 255) <= 129);
    assert((XGetPixel(pixels, x + 25, y + 25) >> 24) == 255);
    assert((XGetPixel(pixels, x + 55, y + 25) >> 24) == 0);
    /* All visible title/border pixels must remain opaque, including glyphs. */
    for (int row = 0; row < fa.height; row++) {
        for (int col = 0; col < fa.width; col++) {
            if (col >= x && col < x + ca.width && row >= y && row < y + ca.height)
                continue;
            assert((XGetPixel(pixels, col, row) >> 24) == 255);
        }
    }
    XDestroyImage(pixels);
    XFreePixmap(d, pixmap);
    XTranslateCoordinates(d, w, root, 0, 0, &cx, &cy, &child);
    pixels = XGetImage(d, root, cx, cy, 80, 50, AllPlanes, ZPixmap);
    assert(pixels);
    /* The test runner provides an opaque blue window behind the client. */
    pixel = XGetPixel(pixels, 10, 10) & 0xffffff;
    printf("picom blended pixel=%#lx (expected approximately 0x80007f)\n", pixel);
    assert(((pixel >> 16) & 255) >= 127 && ((pixel >> 16) & 255) <= 129);
    assert((pixel & 255) >= 126 && (pixel & 255) <= 128);
    assert((XGetPixel(pixels, 25, 25) & 0xffffff) == 0xffffff);
    assert((XGetPixel(pixels, 55, 25) & 0xffffff) == 0x0000ff);
    XDestroyImage(pixels);
    puts("PASS: ARGB pixels preserved, text/decorations opaque, picom blends background");
}

static void fullscreen(Window w, Bool enable)
{
    XEvent event = {0};
    event.xclient.type = ClientMessage;
    event.xclient.window = w;
    event.xclient.message_type = XInternAtom(d, "_NET_WM_STATE", False);
    event.xclient.format = 32;
    event.xclient.data.l[0] = enable ? 1 : 0;
    event.xclient.data.l[1] = XInternAtom(d, "_NET_WM_STATE_FULLSCREEN", False);
    event.xclient.data.l[3] = 1;
    XSendEvent(d, root, False, SubstructureRedirectMask | SubstructureNotifyMask, &event);
    settle();
}

static void button_position(Window w, int index, int *x, int *y, int *height)
{
    Window f = frame(w), child;
    XWindowAttributes fa, ca;
    int cx, cy, fx, fy, border;
    assert(f != w);
    XGetWindowAttributes(d, f, &fa);
    XGetWindowAttributes(d, w, &ca);
    XTranslateCoordinates(d, w, root, 0, 0, &cx, &cy, &child);
    XTranslateCoordinates(d, f, root, 0, 0, &fx, &fy, &child);
    border = cx - fx;
    *height = cy - fy - border;
    assert(*height > 0 && *height < 100);
    *x = fx + fa.width - border - (*height * index) - *height / 2;
    *y = fy + border + *height / 2;
}

static void move_pointer(int x, int y)
{
    XTestFakeMotionEvent(d, DefaultScreen(d), x, y, CurrentTime);
    settle();
}

static void button(int number, Bool down)
{
    XTestFakeButtonEvent(d, number, down, CurrentTime);
    settle();
}

static void click(Window w, int index)
{
    int x, y, height;
    button_position(w, index, &x, &y, &height);
    move_pointer(x, y);
    button(1, True);
    button(1, False);
}

static int deletes(Window w)
{
    XEvent e;
    int count = 0;
    settle();
    while (XPending(d)) {
        XNextEvent(d, &e);
        if (e.type == ClientMessage && e.xclient.window == w &&
            e.xclient.message_type == protocols && e.xclient.format == 32 &&
            (Atom)e.xclient.data.l[0] == delete_window) {
            assert(e.xclient.data.l[1] != CurrentTime);
            ++count;
        }
    }
    return count;
}

static long window_state(Window w)
{
    Atom type;
    int format;
    unsigned long count, after;
    unsigned char *value = NULL;
    long result = -1;
    XGetWindowProperty(d, w, state, 0, 2, False, state,
                       &type, &format, &count, &after, &value);
    if (value && count) result = *(long *)value;
    if (value) XFree(value);
    return result;
}

static void destroy(Window w)
{
    XDestroyWindow(d, w);
    settle();
}

static int killed(Display *display)
{
    (void)display;
    _exit(77);
}

static void test_fallback(void)
{
    int pipefd[2], status, i;
    pid_t child;
    Window w;
    assert(pipe(pipefd) == 0);
    child = fork();
    assert(child >= 0);
    if (!child) {
        XEvent event;
        close(pipefd[0]);
        d = XOpenDisplay(NULL);
        assert(d);
        XSetIOErrorHandler(killed);
        w = create_window(ALL, ALL, 300);
        XDeleteProperty(d, w, protocols);
        XSync(d, False);
        assert(write(pipefd[1], &w, sizeof(w)) == sizeof(w));
        for (;;) XNextEvent(d, &event);
    }
    close(pipefd[1]);
    assert(read(pipefd[0], &w, sizeof(w)) == sizeof(w));
    close(pipefd[0]);
    settle();
    click(w, 0);
    for (i = 0; i < 30; ++i) {
        if (waitpid(child, &status, WNOHANG) == child) {
            assert(WIFEXITED(status) && WEXITSTATUS(status) == 77);
            puts("PASS: client without WM_DELETE_WINDOW is disconnected");
            return;
        }
        settle();
    }
    kill(child, SIGTERM);
    waitpid(child, &status, 0);
    assert(!"fallback did not close the client");
}

int main(void)
{
    Window w;
    XWindowAttributes before, after;
    int x, y, h, i;
    int matte = getenv("MATTE_WIDTH") ? atoi(getenv("MATTE_WIDTH")) : 0;
    long decorations[] = {ALL, TITLE, TITLE | MENU, TITLE | MIN_BUTTON,
                          TITLE | MAX_BUTTON, TITLE | MIN_BUTTON | MAX_BUTTON};
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(60);
    d = XOpenDisplay(NULL);
    assert(d);
    root = DefaultRootWindow(d);
    protocols = XInternAtom(d, "WM_PROTOCOLS", False);
    delete_window = XInternAtom(d, "WM_DELETE_WINDOW", False);
    hints = XInternAtom(d, "_MOTIF_WM_HINTS", False);
    state = XInternAtom(d, "WM_STATE", False);

    if (getenv("ARGB_CLIENTS")) {
        XSetWindowAttributes attrs = {0};
        attrs.override_redirect = True;
        attrs.background_pixel = 0x0000ff;
        Window backdrop = XCreateWindow(d, root, 0, 0, 1024, 768, 0, CopyFromParent,
                                         InputOutput, CopyFromParent,
                                         CWOverrideRedirect | CWBackPixel, &attrs);
        XMapWindow(d, backdrop);
        XRaiseWindow(d, backdrop);
        settle();
    }
    w = create_window(ALL, ALL, 360);
    if (getenv("ARGB_CLIENTS")) check_argb(w);
    button_position(w, 0, &x, &y, &h);
    move_pointer(x, y);
    button(1, True);
    assert(deletes(w) == 0); /* Must wait for release. */
    if (getenv("EMWM_TEST_PAUSE")) sleep(5);
    move_pointer(x - 3 * h, y);
    button(1, False);
    assert(deletes(w) == 0);
    move_pointer(x, y);
    button(3, True);
    button(3, False);
    assert(deletes(w) == 0);
    click(w, 0);
    assert(deletes(w) == 1);
    assert(window_state(w) == NormalState); /* Client decides when to exit. */
    puts("PASS: close on release; drag cancellation; WM_DELETE_WINDOW exactly once");

    XGetWindowAttributes(d, w, &before);
    click(w, 1);
    XGetWindowAttributes(d, w, &after);
    assert(after.width > before.width);
    if (getenv("ARGB_CLIENTS")) check_argb(w);
    click(w, 0);
    assert(deletes(w) == 1);
    click(w, 1);
    XGetWindowAttributes(d, w, &after);
    assert(after.width == before.width && after.height == before.height);
    click(w, 2);
    assert(window_state(w) == IconicState);
    XMapWindow(d, w);
    settle();
    assert(window_state(w) == NormalState);
    puts("PASS: maximize, restore, close while maximized, minimize");
    if (getenv("ARGB_CLIENTS")) {
        check_argb(w);
        XStoreName(d, w, "ARGB title repaint");
        XClearArea(d, frame(w), 0, 0, 0, 0, True);
        settle();
        check_argb(w);
        fullscreen(w, True);
        check_argb(w);
        fullscreen(w, False);
        check_argb(w);
    }


    set_hints(w, ALL | CLOSE, ALL);
    click(w, 0); /* Rightmost button is now maximize. */
    assert(deletes(w) == 0);
    XGetWindowAttributes(d, w, &after);
    assert(after.width > before.width);
    click(w, 0);
    set_hints(w, ALL, ALL);
    click(w, 0);
    assert(deletes(w) == 1);
    puts("PASS: dynamic Motif close restriction removes and restores button");
    destroy(w);

    for (i = 0; i < (int)(sizeof(decorations) / sizeof(decorations[0])); ++i) {
        w = create_window(ALL, decorations[i], 1);
        click(w, 0);
        assert(deletes(w) == 1);
        destroy(w);
    }
    puts("PASS: narrow windows and title/menu/minimize/maximize combinations");
    w = create_window(ALL | CLOSE, ALL, 1);
    set_hints(w, ALL, ALL);
    button_position(w, 0, &x, &y, &h);
    XGetWindowAttributes(d, w, &after);
    assert(after.width + 2 * matte >= 5 * h);
    click(w, 0);
    assert(deletes(w) == 1);
    destroy(w);
    puts("PASS: enabling close on an already narrow window reserves title space");


    w = create_window(MOVE, TITLE, 300);
    click(w, 0);
    assert(deletes(w) == 0);
    destroy(w);
    w = create_window(ALL, 0, 300);
    XGetWindowAttributes(d, frame(w), &before);
    XGetWindowAttributes(d, w, &after);
    assert(before.height == after.height + 2 * matte &&
           before.width == after.width + 2 * matte);
    destroy(w);
    puts("PASS: no close function and undecorated windows");
    test_fallback();
    XCloseDisplay(d);
    return 0;
}
