/* Real keyboard/geometry regression tests; private X server only. */
#define _POSIX_C_SOURCE 200809L
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/keysym.h>
#include <X11/XKBlib.h>
#include <X11/extensions/XTest.h>
#include <X11/extensions/Xrandr.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
static Display *d;
static Window root;
typedef struct { int x, y, w, h; } Rect;
static void settle(void) {
    struct timespec delay={0,130000000}; XSync(d,False);
    nanosleep(&delay,NULL); XSync(d,False);
}
static Window frame(Window w) {
    Window r,p,*kids; unsigned int n;
    for (;;) {
        assert(XQueryTree(d,w,&r,&p,&kids,&n)); if(kids) XFree(kids);
        if(p==root) return w;
        w=p;
    }
}
static Rect rect(Window w) {
    XWindowAttributes a; assert(XGetWindowAttributes(d,frame(w),&a));
    return (Rect){a.x,a.y,a.width+2*a.border_width,a.height+2*a.border_width};
}
static void maximized(Window w,int expected) {
    Atom type, atom=XInternAtom(d,"_NET_WM_STATE",False);
    Atom horizontal=XInternAtom(d,"_NET_WM_STATE_MAXIMIZED_HORZ",False);
    Atom vertical=XInternAtom(d,"_NET_WM_STATE_MAXIMIZED_VERT",False);
    int format, found=0; unsigned long n,remaining; unsigned char *data=NULL;
    assert(XGetWindowProperty(d,w,atom,0,64,False,XA_ATOM,&type,&format,&n,&remaining,&data)==Success);
    if(type==XA_ATOM && format==32) for(unsigned long i=0;i<n;i++) {
        Atom a=((Atom*)data)[i]; if(a==horizontal || a==vertical) found++;
    }
    if(data) XFree(data);
    assert(found==(expected?2:0));
}
static void equal(Rect a,Rect b) {
    if(a.x!=b.x || a.y!=b.y || a.w!=b.w || a.h!=b.h) {
        fprintf(stderr,"geometry got %d,%d %dx%d expected %d,%d %dx%d\n",a.x,a.y,a.w,a.h,b.x,b.y,b.w,b.h);
        abort();
    }
}
static void inside(Rect a,Rect b) {
    assert(a.x>=b.x && a.y>=b.y && a.x+a.w<=b.x+b.w && a.y+a.h<=b.y+b.h);
}
static void prop(Window w,const char *name,Atom type,unsigned long *data,int n) {
    XChangeProperty(d,w,XInternAtom(d,name,False),type,32,PropModeReplace,(unsigned char*)data,n);
}
static void focus(Window w) { XSetInputFocus(d,w,RevertToPointerRoot,CurrentTime); settle(); }
static void key(KeySym sym,int ctrl) {
    KeyCode super=XKeysymToKeycode(d,XK_Super_L),code=XKeysymToKeycode(d,sym);
    KeyCode control=XKeysymToKeycode(d,XK_Control_L);
    assert(super && code && control);
    XTestFakeKeyEvent(d,super,True,CurrentTime);
    if(ctrl) XTestFakeKeyEvent(d,control,True,CurrentTime);
    XTestFakeKeyEvent(d,code,True,CurrentTime); XTestFakeKeyEvent(d,code,False,CurrentTime);
    if(ctrl) XTestFakeKeyEvent(d,control,False,CurrentTime);
    XTestFakeKeyEvent(d,super,False,CurrentTime); settle();
}
static void plain_alt(KeySym sym) {
    KeyCode alt=XKeysymToKeycode(d,XK_Alt_L),code=XKeysymToKeycode(d,sym);
    XTestFakeKeyEvent(d,alt,True,CurrentTime);
    XTestFakeKeyEvent(d,code,True,CurrentTime); XTestFakeKeyEvent(d,code,False,CurrentTime);
    XTestFakeKeyEvent(d,alt,False,CurrentTime); settle();
}
static Window create(XSizeHints *hints,Window transient,const char *type,long functions) {
    Window w=XCreateSimpleWindow(d,root,120,100,320,220,0,0,0xb0b0b0);
    XSizeHints normal={0};
    if(hints) normal=*hints;
    normal.flags|=USPosition;
    XSetWMNormalHints(d,w,&normal);
    XStoreName(d,w,"EMWM tiling test");
    if(transient) XSetTransientForHint(d,w,transient);
    if(type) { unsigned long a=XInternAtom(d,type,False); prop(w,"_NET_WM_WINDOW_TYPE",XA_ATOM,&a,1); }
    if(functions>=0) { unsigned long v[5]={1,functions,0,0,0};
        Atom a=XInternAtom(d,"_MOTIF_WM_HINTS",False); prop(w,"_MOTIF_WM_HINTS",a,v,5); }
    XMapWindow(d,w); settle(); focus(w); return w;
}
static void destroy(Window w) { XDestroyWindow(d,w); settle(); }
static Window panel(int height,int start,int end,int legacy) {
    XSetWindowAttributes a={.override_redirect=True};
    Window w=XCreateWindow(d,root,0,0,1,1,0,CopyFromParent,InputOutput,CopyFromParent,CWOverrideRedirect,&a);
    unsigned long s[12]={0,0,height,0,0,0,0,0,start,end,0,0};
    prop(w,legacy?"_NET_WM_STRUT":"_NET_WM_STRUT_PARTIAL",XA_CARDINAL,s,legacy?4:12);
    XMapWindow(d,w); settle(); return w;
}
static void rejected(Window w) {
    Rect before=rect(w);
    key(XK_Left,0); equal(rect(w),before);
    key(XK_Up,0); equal(rect(w),before);
    key(XK_Down,0); equal(rect(w),before);
    destroy(w);
}
int main(void) {
    d=XOpenDisplay(NULL); assert(d); root=DefaultRootWindow(d);
    int sw=DisplayWidth(d,DefaultScreen(d)),sh=DisplayHeight(d,DefaultScreen(d));
    Rect screen={0,0,sw,sh},left={0,0,sw/2,sh},right={sw/2,0,sw-sw/2,sh};
    Window w=create(NULL,None,NULL,-1);
    Rect original=rect(w);
    if(getenv("NO_TILE_BINDINGS")) {
        key(XK_Left,0); equal(rect(w),original); destroy(w);
        puts("PASS: custom key binding set can disable all tiling shortcuts"); return 0;
    }
    key(XK_Left,0); equal(rect(w),left);
    key(XK_Up,0); equal(rect(w),(Rect){0,0,sw/2,sh/2});
    key(XK_Down,0); equal(rect(w),original);
    key(XK_Right,0); equal(rect(w),right);
    key(XK_Down,0); equal(rect(w),(Rect){sw/2,sh/2,sw-sw/2,sh-sh/2});
    key(XK_Down,0); equal(rect(w),original);
    key(XK_Left,0); key(XK_Down,0); equal(rect(w),(Rect){0,sh/2,sw/2,sh-sh/2});
    key(XK_Down,0); equal(rect(w),original);
    key(XK_Right,0); key(XK_Up,0); equal(rect(w),(Rect){sw/2,0,sw-sw/2,sh/2});
    key(XK_Up,0); equal(rect(w),screen); maximized(w,1);
    key(XK_Down,0); equal(rect(w),original); maximized(w,0);
    if(getenv("DEFAULT_TILE_BINDINGS")) {
        destroy(w); puts("PASS: built-in/default key set, quadrant sequence and EWMH state"); return 0;
    }
    key(XK_Up,0); equal(rect(w),screen);
    key(XK_Left,0); equal(rect(w),left);
    key(XK_Right,0); equal(rect(w),right);
    key(XK_r,1); equal(rect(w),original);
    puts("PASS: halves, all quarters, maximized transitions and original restore point");

    unsigned int num=XkbKeysymToModifiers(d,XK_Num_Lock);
    assert(num);
    for(int enabled=0; enabled<2; enabled++) {
        XkbLockModifiers(d,XkbUseCoreKbd,num|LockMask,enabled?(num|LockMask):0); settle();
        key(XK_KP_Left,0); equal(rect(w),left);
        key(XK_KP_Up,0); equal(rect(w),(Rect){0,0,sw/2,sh/2});
        key(XK_KP_Down,0); equal(rect(w),original);
        key(XK_KP_Right,0); equal(rect(w),right);
        key(XK_KP_Down,0); equal(rect(w),(Rect){sw/2,sh/2,sw-sw/2,sh-sh/2});
        key(XK_KP_Down,0); equal(rect(w),original);
        key(XK_Up,0); equal(rect(w),screen); key(XK_Down,0); equal(rect(w),original);
    }
    XkbLockModifiers(d,XkbUseCoreKbd,num|LockMask,0); settle();
    puts("PASS: keypad and ordinary arrows with Num Lock/Caps Lock on and off");
    key(XK_Home,1); equal(rect(w),(Rect){0,0,sw/2,sh/2}); key(XK_r,1); equal(rect(w),original);
    key(XK_Prior,1); equal(rect(w),(Rect){sw/2,0,sw-sw/2,sh/2}); key(XK_r,1);
    key(XK_End,1); equal(rect(w),(Rect){0,sh/2,sw/2,sh-sh/2}); key(XK_r,1);
    key(XK_Next,1); equal(rect(w),(Rect){sw/2,sh/2,sw-sw/2,sh-sh/2}); key(XK_r,1);
    puts("PASS: configurable direct quadrant and restore bindings");

    key(XK_Left,0);
    XWindowAttributes unchanged; XGetWindowAttributes(d,w,&unchanged);
    XResizeWindow(d,w,unchanged.width,unchanged.height); settle();
    key(XK_r,1); equal(rect(w),original);
    key(XK_Left,0); plain_alt(XK_F10); plain_alt(XK_F5); equal(rect(w),left);
    key(XK_r,1); equal(rect(w),original);
    key(XK_Left,0); plain_alt(XK_F9);
    XMapWindow(d,w); settle(); focus(w); equal(rect(w),left); key(XK_r,1); equal(rect(w),original);
    key(XK_Left,0); XMoveResizeWindow(d,w,170,150,350,240); settle();
    Rect moved=rect(w); key(XK_r,1); equal(rect(w),moved);
    key(XK_Right,0); key(XK_r,1); equal(rect(w),moved);
    puts("PASS: traditional maximize/minimize/restore and manual reconfiguration");

    Window bar=panel(40,0,sw-1,0);
    key(XK_Left,0); equal(rect(w),(Rect){0,40,sw/2,sh-40}); key(XK_r,1);
    key(XK_Up,0); equal(rect(w),(Rect){0,40,sw,sh-40}); key(XK_Down,0);
    unsigned long s[12]={0,0,60,0,0,0,0,0,0,sw-1,0,0};
    prop(bar,"_NET_WM_STRUT_PARTIAL",XA_CARDINAL,s,12); settle();
    key(XK_Right,0); equal(rect(w),(Rect){sw/2,60,sw-sw/2,sh-60}); key(XK_r,1);
    XUnmapWindow(d,bar); settle(); key(XK_Left,0); equal(rect(w),left); key(XK_r,1); destroy(bar);
    bar=panel(30,0,sw-1,1); key(XK_Up,0); equal(rect(w),(Rect){0,30,sw,sh-30}); key(XK_Down,0); destroy(bar);
    puts("PASS: EWMH partial/legacy struts, changes and hidden panels");
    destroy(w);

    XSizeHints hints={0}; hints.flags=PMinSize|PMaxSize;
    hints.min_width=hints.max_width=320; hints.min_height=hints.max_height=220;
    rejected(create(&hints,None,NULL,-1));
    rejected(create(NULL,None,NULL,4|8|16|32)); /* no resize */
    rejected(create(NULL,None,NULL,2|8|16|32)); /* no move */
    rejected(create(NULL,None,"_NET_WM_WINDOW_TYPE_DIALOG",-1));
    rejected(create(NULL,None,"_NET_WM_WINDOW_TYPE_DOCK",-1));
    Window leader=create(NULL,None,NULL,-1);
    rejected(create(NULL,leader,NULL,-1)); destroy(leader);
    w=create(NULL,None,NULL,2|4|8|32); original=rect(w);
    key(XK_Up,0); equal(rect(w),original); key(XK_Left,0); equal(rect(w),left);
    key(XK_r,1); equal(rect(w),original); destroy(w);
    puts("PASS: fixed windows, Motif restrictions, dialogs, docks and transients excluded");

    w=create(NULL,None,NULL,-1);
    unsigned long no_decor[5]={2,0,0,0,0};
    Atom motif=XInternAtom(d,"_MOTIF_WM_HINTS",False);
    prop(w,"_MOTIF_WM_HINTS",motif,no_decor,5); settle();
    key(XK_Right,0); equal(rect(w),right); key(XK_r,1); destroy(w);
    w=XCreateSimpleWindow(d,root,120,100,320,220,3,0,0xb0b0b0);
    XMapWindow(d,w); settle(); focus(w);
    original=rect(w); key(XK_Left,0); equal(rect(w),left);
    key(XK_r,1); equal(rect(w),original); destroy(w);
    puts("PASS: undecorated windows and nonzero X11 client borders");

    hints=(XSizeHints){0}; hints.flags=PMinSize; hints.min_width=sw/2+1; hints.min_height=1;
    w=create(&hints,None,NULL,-1); original=rect(w); key(XK_Left,0); equal(rect(w),original); destroy(w);
    hints=(XSizeHints){0}; hints.flags=PMinSize|PMaxSize|PResizeInc|PBaseSize;
    hints.min_width=100; hints.min_height=80; hints.max_width=400; hints.max_height=500;
    hints.base_width=10; hints.base_height=10; hints.width_inc=13; hints.height_inc=17;
    w=create(&hints,None,NULL,-1); key(XK_Right,0); inside(rect(w),right);
    XWindowAttributes ca; XGetWindowAttributes(d,w,&ca);
    assert(ca.width<=400 && ca.height<=500 && (ca.width-10)%13==0 && (ca.height-10)%17==0);
    destroy(w);
    hints=(XSizeHints){0}; hints.flags=PAspect; hints.min_aspect.x=hints.max_aspect.x=4;
    hints.min_aspect.y=hints.max_aspect.y=3;
    w=create(&hints,None,NULL,-1); key(XK_Left,0); inside(rect(w),left);
    XGetWindowAttributes(d,w,&ca); assert(ca.width*3==ca.height*4); destroy(w);
    puts("PASS: minimum/maximum sizes, resize increments and exact aspect ratio");

    /* Real RandR logical monitors on the private X server. */
    int count; XRRScreenResources *res=XRRGetScreenResourcesCurrent(d,root); assert(res && res->noutput);
    XRRMonitorInfo m={0}; m.name=XInternAtom(d,"EMWM-test-left",False);
    m.width=sw/2; m.height=sh; m.mwidth=150; m.mheight=200;
    m.noutput=1; m.outputs=&res->outputs[0]; XRRSetMonitor(d,root,&m);
    m.name=XInternAtom(d,"EMWM-test-right",False); m.x=sw/2; m.width=sw-sw/2;
    m.noutput=0; m.outputs=NULL; XRRSetMonitor(d,root,&m); settle();
    XRRMonitorInfo *monitors=XRRGetMonitors(d,root,False,&count);
    assert(monitors && count==2); XRRFreeMonitors(monitors); XRRFreeScreenResources(res);
    w=create(NULL,None,NULL,-1); XMoveWindow(d,w,sw/2+50,100); settle(); focus(w);
    original=rect(w); key(XK_Left,0);
    equal(rect(w),(Rect){sw/2,0,(sw-sw/2)/2,sh}); key(XK_r,1); equal(rect(w),original);
    bar=panel(40,0,sw/2-1,0);
    key(XK_Up,0); equal(rect(w),right); key(XK_Down,0);
    XMoveWindow(d,w,50,100); settle(); key(XK_Up,0);
    equal(rect(w),(Rect){0,40,sw/2,sh-40}); key(XK_Down,0);
    /* Top-left is on left monitor, but most of the window is on right. */
    XMoveResizeWindow(d,w,sw/2-50,100,320,220); settle();
    key(XK_Up,0); equal(rect(w),right); key(XK_Down,0);
    destroy(bar);
    XRRDeleteMonitor(d,root,XInternAtom(d,"EMWM-test-right",False));
    XRRDeleteMonitor(d,root,XInternAtom(d,"EMWM-test-left",False)); settle();
    key(XK_Up,0); equal(rect(w),screen); key(XK_Down,0); destroy(w);
    puts("PASS: two RandR monitors, largest intersection, per-monitor panel and monitor removal");
    XCloseDisplay(d); return 0;
}
