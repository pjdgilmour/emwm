/* ARGB client frames. Decorations retain the screen's native Motif drawing depth.
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#ifndef _WmArgb_h
#define _WmArgb_h

#include <X11/extensions/Xrender.h>

typedef struct {
    Pixmap pixmap;
    Picture source;
    Picture target;
    unsigned int width, height;
} ArgbBuffer;

typedef struct _ArgbFrame {
    Visual *visual;
    int depth;
    Colormap colormap;
    GC backgroundGC;
    ArgbBuffer frame, base;
} ArgbFrame;

void InitArgbFrame(ClientData *pcd);
Drawable ClientDecorDrawable(ClientData *pcd, Boolean base);
void PaintArgbBackground(ClientData *pcd, Boolean base, XRectangle *area);
void PresentArgbDecoration(ClientData *pcd, Boolean base);
void FreeArgbFrame(ClientData *pcd);

#endif
