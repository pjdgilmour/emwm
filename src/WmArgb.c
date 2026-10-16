/*
 * ARGB client frame support for EMWM.
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Keep the client's alpha through the reparenting hierarchy. Motif GCs,
 * tiled backgrounds and font rendering still use the screen's default depth:
 * draw decorations there, then copy only their opaque regions with XRender.
 */
#include "WmGlobal.h"
#include "WmArgb.h"
#include "WmCDInfo.h"

void InitArgbFrame(ClientData *pcd)
{
    XWindowAttributes attrs;
    XRenderPictFormat *format;
    int event, error;
    ArgbFrame *argb;

    if (!XRenderQueryExtension(DISPLAY, &event, &error) ||
        !XGetWindowAttributes(DISPLAY, pcd->client, &attrs)) return;
    format = XRenderFindVisualFormat(DISPLAY, attrs.visual);
    if (!format || format->type != PictTypeDirect || !format->direct.alphaMask)
        return;

    argb = (ArgbFrame *)XtCalloc(1, sizeof(ArgbFrame));
    if (!argb) return;
    argb->visual = attrs.visual;
    argb->depth = attrs.depth;
    argb->colormap = XCreateColormap(DISPLAY, ROOT_FOR_CLIENT(pcd),
                                    attrs.visual, AllocNone);
    pcd->argbFrame = argb;
}

Drawable ClientDecorDrawable(ClientData *pcd, Boolean base)
{
    ArgbFrame *argb = pcd->argbFrame;
    Window window = base ? pcd->clientBaseWin : pcd->clientFrameWin;
    ArgbBuffer *buffer;
    unsigned int width, height;
    int screen = SCREEN_FOR_CLIENT(pcd);

    if (!argb) return window;
    buffer = base ? &argb->base : &argb->frame;
    width = base ? BaseWindowWidth(pcd) : pcd->frameInfo.width;
    height = base ? BaseWindowHeight(pcd) : pcd->frameInfo.height;
    if (buffer->pixmap && (width != buffer->width || height != buffer->height)) {
        XRenderFreePicture(DISPLAY, buffer->source);
        XFreePixmap(DISPLAY, buffer->pixmap);
        buffer->pixmap = None;
    }
    if (!buffer->pixmap) {
        buffer->width = width;
        buffer->height = height;
        buffer->pixmap = XCreatePixmap(DISPLAY, ROOT_FOR_CLIENT(pcd), width, height,
                                       DefaultDepth(DISPLAY, screen));
        buffer->source = XRenderCreatePicture(DISPLAY, buffer->pixmap,
            XRenderFindVisualFormat(DISPLAY, DefaultVisual(DISPLAY, screen)), 0, NULL);
        if (!argb->backgroundGC)
            argb->backgroundGC = XCreateGC(DISPLAY, buffer->pixmap, 0, NULL);
    }
    if (!buffer->target)
        buffer->target = XRenderCreatePicture(DISPLAY, window,
            XRenderFindVisualFormat(DISPLAY, argb->visual), 0, NULL);
    return buffer->pixmap;
}

void PaintArgbBackground(ClientData *pcd, Boolean base, XRectangle *area)
{
    ArgbFrame *argb = pcd->argbFrame;
    ArgbBuffer *buffer;
    Pixmap tile;
    Pixel pixel;
    Drawable drawable;
    XGCValues values;
    unsigned long mask = GCForeground | GCFillStyle;

    if (!argb) return;
    drawable = ClientDecorDrawable(pcd, base);
    buffer = base ? &argb->base : &argb->frame;
    if (base) {
        tile = None;
        pixel = pcd->matteBackground;
    }
    else if (pcd == wmGD.keyboardFocus) {
        tile = CLIENT_APPEARANCE(pcd).activeBackgroundPixmap;
        pixel = CLIENT_APPEARANCE(pcd).activeBackground;
    }
    else {
        tile = CLIENT_APPEARANCE(pcd).backgroundPixmap;
        pixel = CLIENT_APPEARANCE(pcd).background;
    }
    values.foreground = pixel;
    values.fill_style = tile ? FillTiled : FillSolid;
    if (tile) {
        values.tile = tile;
        mask |= GCTile;
    }
    XChangeGC(DISPLAY, argb->backgroundGC, mask, &values);
    XFillRectangle(DISPLAY, drawable, argb->backgroundGC,
                   area ? area->x : 0, area ? area->y : 0,
                   area ? area->width : buffer->width,
                   area ? area->height : buffer->height);
}

static void CopyDecoration(ArgbBuffer *buffer, int x, int y, int width, int height)
{
    if (width <= 0 || height <= 0) return;
    XRenderComposite(DISPLAY, PictOpSrc, buffer->source, None, buffer->target,
                     x, y, 0, 0, x, y, width, height);
}

void PresentArgbDecoration(ClientData *pcd, Boolean base)
{
    ArgbFrame *argb = pcd->argbFrame;
    ArgbBuffer *buffer;
    int x, y, width, height;

    if (!argb) return;
    buffer = base ? &argb->base : &argb->frame;
    if (!buffer->pixmap) return;
    if (base) {
        x = y = pcd->matteWidth;
        width = buffer->width - 2 * x;
        height = buffer->height - 2 * y;
    }
    else {
        x = BaseWindowX(pcd);
        y = BaseWindowY(pcd);
        width = BaseWindowWidth(pcd);
        height = BaseWindowHeight(pcd);
    }
    /* Never paint across the client: its alpha belongs to the application. */
    CopyDecoration(buffer, 0, 0, buffer->width, y);
    CopyDecoration(buffer, 0, y, x, height);
    CopyDecoration(buffer, x + width, y, buffer->width - x - width, height);
    CopyDecoration(buffer, 0, y + height, buffer->width, buffer->height - y - height);
}

void FreeArgbFrame(ClientData *pcd)
{
    ArgbFrame *argb = pcd->argbFrame;
    ArgbBuffer *buffers[2];
    int i;

    if (!argb) return;
    buffers[0] = &argb->frame;
    buffers[1] = &argb->base;
    for (i = 0; i < 2; i++) {
        if (buffers[i]->target) XRenderFreePicture(DISPLAY, buffers[i]->target);
        if (buffers[i]->source) XRenderFreePicture(DISPLAY, buffers[i]->source);
        if (buffers[i]->pixmap) XFreePixmap(DISPLAY, buffers[i]->pixmap);
    }
    if (argb->backgroundGC) XFreeGC(DISPLAY, argb->backgroundGC);
    XFreeColormap(DISPLAY, argb->colormap);
    XtFree((char *)argb);
    pcd->argbFrame = NULL;
}
