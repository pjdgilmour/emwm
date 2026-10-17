/* SPDX-License-Identifier: LGPL-2.0-or-later
 * Keyboard tiling for floating windows. All changes use the existing Motif
 * configuration/state machinery; no extra windows, grabs, timers or daemon.
 */
#include "WmGlobal.h"
#include "WmFunction.h"
#include "WmEvent.h"
#include "WmWinConf.h"
#include "WmWinInfo.h"
#include "WmWinState.h"
#include "WmXinerama.h"
#include "WmWrkspace.h"
#include <X11/Xatom.h>
#include <X11/extensions/Xrandr.h>
#include <string.h>
#include <limits.h>

#define MIN(a,b) ((a) < (b) ? (a) : (b))
#define MAX(a,b) ((a) > (b) ? (a) : (b))

/* Zero also means that no restore geometry has been saved. */
enum { TILE_NONE, TILE_LEFT, TILE_RIGHT, TILE_TL, TILE_TR,
       TILE_BL, TILE_BR, TILE_MAX };
typedef struct { int x, y, width, height; } TileRect;
static Atom typeAtom, normalAtom, specialAtoms[13], strutAtom, partialAtom,
            desktopAtom;

static void InitTileAtoms(void)
{
    static const char *names[] = {"DESKTOP", "DOCK", "TOOLBAR", "MENU",
        "UTILITY", "SPLASH", "DIALOG", "DROPDOWN_MENU", "POPUP_MENU",
        "TOOLTIP", "NOTIFICATION", "COMBO", "DND"};
    unsigned int i;
    char name[80];
    if (typeAtom) return;
    typeAtom = XInternAtom(DISPLAY, "_NET_WM_WINDOW_TYPE", False);
    normalAtom = XInternAtom(DISPLAY, "_NET_WM_WINDOW_TYPE_NORMAL", False);
    strutAtom = XInternAtom(DISPLAY, "_NET_WM_STRUT", False);
    partialAtom = XInternAtom(DISPLAY, "_NET_WM_STRUT_PARTIAL", False);
    desktopAtom = XInternAtom(DISPLAY, "_NET_WM_DESKTOP", False);
    for (i = 0; i < XtNumber(names); i++) {
        snprintf(name, sizeof(name), "_NET_WM_WINDOW_TYPE_%s", names[i]);
        specialAtoms[i] = XInternAtom(DISPLAY, name, False);
    }
}

/* Bounded, type-checked reads, including malformed/disappearing clients. */
static int ReadWords(Window window, Atom property, Atom type,
                     unsigned long *words, int capacity)
{
    Atom actual;
    int format, count = 0;
    unsigned long n, remaining;
    unsigned char *data = NULL;
    if (XGetWindowProperty(DISPLAY, window, property, 0, capacity, False,
            type, &actual, &format, &n, &remaining, &data) == Success &&
            actual == type && format == 32 && !remaining && n <= (unsigned int)capacity) {
        count = (int)n;
        if (count) memcpy(words, data, n * sizeof(*words));
    }
    if (data) XFree(data);
    return count;
}

static Boolean CanTile(ClientData *cd)
{
    unsigned long types[32];
    int i, n;
    unsigned int j;
    if (!cd || (cd->clientState != NORMAL_STATE &&
                cd->clientState != MAXIMIZED_STATE) || cd->fullScreen ||
        cd->transientFor || cd->transientLeader ||
        cd->inputMode != MWM_INPUT_MODELESS ||
        (cd->clientFlags & CLIENT_WM_CLIENTS) ||
        (cd->clientFunctions & (MWM_FUNC_MOVE|MWM_FUNC_RESIZE)) !=
                              (MWM_FUNC_MOVE|MWM_FUNC_RESIZE) ||
        (cd->minWidth >= cd->maxWidthLimit &&
         cd->minHeight >= cd->maxHeightLimit)) return False;
    n = ReadWords(cd->client, typeAtom, XA_ATOM, types, XtNumber(types));
    for (i = 0; i < n; i++) {
        if (types[i] == normalAtom) return True;
        for (j = 0; j < XtNumber(specialAtoms); j++)
            if (types[i] == specialAtoms[j]) return False;
    }
    return True; /* Traditional ICCCM clients need no EWMH properties. */
}

static long long Intersection(TileRect a, TileRect b)
{
    int w = MIN(a.x + a.width, b.x + b.width) - MAX(a.x, b.x);
    int h = MIN(a.y + a.height, b.y + b.height) - MAX(a.y, b.y);
    return w > 0 && h > 0 ? (long long)w * h : 0;
}

static void ChooseMonitor(TileRect client, TileRect candidate, TileRect *best,
                          long long *area, long long *distance)
{
    long long overlap = Intersection(client, candidate);
    long long dx = (long long)client.x * 2 + client.width -
                  ((long long)candidate.x * 2 + candidate.width);
    long long dy = (long long)client.y * 2 + client.height -
                  ((long long)candidate.y * 2 + candidate.height);
    long long dist = dx * dx + dy * dy;
    if (candidate.width <= 0 || candidate.height <= 0) return;
    if (overlap > *area || (overlap == *area && dist < *distance)) {
        *area = overlap;
        *distance = dist;
        *best = candidate;
    }
}

static TileRect ClientMonitor(ClientData *cd)
{
    TileRect best = {0, 0, DisplayWidth(DISPLAY, SCREEN_FOR_CLIENT(cd)),
                         DisplayHeight(DISPLAY, SCREEN_FOR_CLIENT(cd))};
    TileRect client = {cd->frameInfo.x, cd->frameInfo.y,
                       cd->frameInfo.width, cd->frameInfo.height};
    long long area = -1, distance = LLONG_MAX;
    int i, n = 0;
    XineramaScreenInfo si;
#if RANDR_MAJOR > 1 || (RANDR_MAJOR == 1 && RANDR_MINOR >= 5)
    int major, minor;
    XRRMonitorInfo *monitors;
    /* RandR 1.5 also handles user-defined logical monitors. Query on demand
     * so hotplug does not leave a cached rectangle pointing off-screen. */
    if (XRRQueryVersion(DISPLAY, &major, &minor) &&
        (major > 1 || (major == 1 && minor >= 5))) {
        monitors = XRRGetMonitors(DISPLAY, cd->pSD->rootWindow, False, &n);
        for (i = 0; monitors && i < n; i++) {
            TileRect r = {monitors[i].x, monitors[i].y,
                          monitors[i].width, monitors[i].height};
            ChooseMonitor(client, r, &best, &area, &distance);
        }
        if (monitors) XRRFreeMonitors(monitors);
        if (area >= 0) return best;
    }
#endif
    GetXineramaScreenCount(&n);
    for (i = 0; i < n; i++) {
        if (GetXineramaScreenInfo(i, &si)) {
            TileRect r = {si.x_org, si.y_org, si.width, si.height};
            ChooseMonitor(client, r, &best, &area, &distance);
        }
    }
    return best;
}

static Boolean Overlaps(unsigned long start, unsigned long end, int pos, int size)
{
    return start <= end && (long long)start < (long long)pos + size &&
           (long long)end >= pos;
}

static TileRect WorkArea(ClientData *cd)
{
    TileRect r = ClientMonitor(cd);
    int left = r.x, top = r.y, right = r.x + r.width, bottom = r.y + r.height;
    int sw = DisplayWidth(DISPLAY, SCREEN_FOR_CLIENT(cd));
    int sh = DisplayHeight(DISPLAY, SCREEN_FOR_CLIENT(cd));
    Window root, parent, *children = NULL;
    unsigned int i, n;
    if (!XQueryTree(DISPLAY, cd->pSD->rootWindow, &root, &parent, &children, &n))
        return r;
    for (i = 0; i < n; i++) {
        ClientData *panel = NULL;
        XWindowAttributes attr;
        Window w = children[i];
        unsigned long s[12] = {0}, desktop;
        if (!XGetWindowAttributes(DISPLAY, w, &attr) || attr.map_state != IsViewable)
            continue;
        if (!XFindContext(DISPLAY, w, wmGD.windowContextType, (XPointer *)&panel))
            w = panel->client;
        if (w == cd->client) continue;
        if (ReadWords(w, desktopAtom, XA_CARDINAL, &desktop, 1) == 1 &&
            desktop != 0xffffffffUL &&
            desktop != (unsigned long)GetCurrentWorkspaceIndex(cd->pSD)) continue;
        if (ReadWords(w, partialAtom, XA_CARDINAL, s, 12) != 12) {
            if (ReadWords(w, strutAtom, XA_CARDINAL, s, 4) != 4) continue;
            s[4] = s[6] = s[8] = s[10] = 0;
            s[5] = s[7] = sh - 1;
            s[9] = s[11] = sw - 1;
        }
        /* EWMH strut depths are measured from ROOT edges, not monitor edges. */
        if (s[0] && s[0] <= (unsigned int)sw && Overlaps(s[4], s[5], r.y, r.height))
            left = MAX(left, (int)s[0]);
        if (s[1] && s[1] <= (unsigned int)sw && Overlaps(s[6], s[7], r.y, r.height))
            right = MIN(right, sw - (int)s[1]);
        if (s[2] && s[2] <= (unsigned int)sh && Overlaps(s[8], s[9], r.x, r.width))
            top = MAX(top, (int)s[2]);
        if (s[3] && s[3] <= (unsigned int)sh && Overlaps(s[10], s[11], r.x, r.width))
            bottom = MIN(bottom, sh - (int)s[3]);
    }
    if (children) XFree(children);
    r.x = left; r.y = top; r.width = right - left; r.height = bottom - top;
    return r;
}

/* Find the largest legal increment-aligned size INSIDE the target. Unlike an
 * interactive resize we must not grow past the region to satisfy an aspect
 * ratio/minimum. If no legal size fits, leave the window unchanged. */
static Boolean FitSize(ClientData *cd, int availableW, int availableH,
                       unsigned int *width, unsigned int *height)
{
    int w, h, maxW = MIN(availableW, cd->maxWidthLimit);
    long long minH;
    int maxH = MIN(availableH, cd->maxHeightLimit);
    int wi = MAX(1, cd->widthInc), hi = MAX(1, cd->heightInc);
    if (maxW < MAX(cd->minWidth, cd->baseWidth) ||
        maxH < MAX(cd->minHeight, cd->baseHeight)) return False;
    maxW -= (maxW - cd->baseWidth) % wi;
    for (w = maxW; w >= MAX(1, cd->minWidth); w -= wi) {
        h = maxH;
        minH = MAX(1, cd->minHeight);
        if (cd->sizeFlags & PAspect) {
            if (cd->minAspect.x > 0 && cd->minAspect.y > 0)
                h = MIN(h, (long long)w * cd->minAspect.y / cd->minAspect.x);
            if (cd->maxAspect.x > 0 && cd->maxAspect.y > 0)
                minH = MAX(minH, ((long long)w * cd->maxAspect.y +
                                 cd->maxAspect.x - 1) / cd->maxAspect.x);
        }
        if (h < cd->baseHeight) continue;
        h -= (h - cd->baseHeight) % hi;
        if (h < minH) continue;
        *width = w; *height = h;
        FixWindowConfiguration(cd, width, height, wi, hi);
        if (*width <= (unsigned int)maxW && *height <= (unsigned int)maxH &&
            *width > 0 && *height > 0)
            return True;
    }
    return False;
}

Boolean F_Tile(String args, ClientData *cd, XEvent *event)
{
    TileRect r;
    unsigned int w, h;
    int mode, x, y, fw, fh;
    Boolean restore = False;
    InitTileAtoms();
    if (!args || !CanTile(cd)) return False;
    if (!strcmp(args, "left")) mode = TILE_LEFT;
    else if (!strcmp(args, "right")) mode = TILE_RIGHT;
    else if (!strcmp(args, "top-left")) mode = TILE_TL;
    else if (!strcmp(args, "top-right")) mode = TILE_TR;
    else if (!strcmp(args, "bottom-left")) mode = TILE_BL;
    else if (!strcmp(args, "bottom-right")) mode = TILE_BR;
    else if (!strcmp(args, "up")) {
        mode = !cd->maxConfig && cd->tileState == TILE_LEFT ? TILE_TL :
               !cd->maxConfig && cd->tileState == TILE_RIGHT ? TILE_TR : TILE_MAX;
    } else if (!strcmp(args, "down")) {
        mode = !cd->maxConfig && cd->tileState == TILE_LEFT ? TILE_BL :
               !cd->maxConfig && cd->tileState == TILE_RIGHT ? TILE_BR : TILE_NONE;
        restore = mode == TILE_NONE;
    } else if (!strcmp(args, "restore")) {
        mode = TILE_NONE; restore = True;
    } else return False;

    if (restore && !cd->tileState) return F_Restore(NULL, cd, event);
    if (mode == TILE_MAX && (!(cd->clientFunctions & MWM_FUNC_MAXIMIZE) ||
                            cd->maxConfig)) return False;
    r = WorkArea(cd);
    if (r.width <= 0 || r.height <= 0) return False;
    fw = 2 * cd->clientOffset.x;
    fh = cd->clientOffset.x + cd->clientOffset.y;
    if (restore) {
        if (!FitSize(cd, MIN(cd->tileWidth, r.width - fw),
                     MIN(cd->tileHeight, r.height - fh), &w, &h)) return False;
        x = MAX(r.x, MIN(cd->tileX - cd->clientOffset.x, r.x + r.width - (int)w - fw));
        y = MAX(r.y, MIN(cd->tileY - cd->clientOffset.y, r.y + r.height - (int)h - fh));
    } else {
        if (mode != TILE_MAX) {
            int half = r.width / 2;
            if (mode == TILE_RIGHT || mode == TILE_TR || mode == TILE_BR) {
                r.x += half; r.width -= half;
            } else r.width = half;
        }
        if (mode >= TILE_TL && mode <= TILE_BR) {
            int half = r.height / 2;
            if (mode == TILE_BL || mode == TILE_BR) {
                r.y += half; r.height -= half;
            } else r.height = half;
        }
        if (!FitSize(cd, r.width - fw, r.height - fh, &w, &h)) return False;
        x = r.x; y = r.y;
        if (mode == TILE_RIGHT || mode == TILE_TR || mode == TILE_BR)
            x += r.width - w - fw;
        if (mode == TILE_BL || mode == TILE_BR) y += r.height - h - fh;
        if (!cd->tileState) {
            /* Normal geometry already contains the restore point when maximized. */
            cd->tileX = cd->clientX; cd->tileY = cd->clientY;
            cd->tileWidth = cd->clientWidth; cd->tileHeight = cd->clientHeight;
        }
    }
    cd->tileApplying = True;
    if (mode == TILE_MAX) {
        cd->maxX = x + cd->clientOffset.x; cd->maxY = y + cd->clientOffset.y;
        cd->oldMaxWidth = cd->maxWidth; cd->oldMaxHeight = cd->maxHeight;
        cd->maxWidth = w; cd->maxHeight = h;
        SetClientState(cd, MAXIMIZED_STATE, GetTimestamp());
    } else {
        if (cd->maxConfig) SetClientState(cd, NORMAL_STATE, GetTimestamp());
        /* clientRequest=True means x/y are client coordinates, not frame ones. */
        ProcessNewConfiguration(cd, x + cd->clientOffset.x,
                                y + cd->clientOffset.y, w, h, True);
    }
    cd->tileApplying = False;
    cd->tileState = mode;
    return False;
}
