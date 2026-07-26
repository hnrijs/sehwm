#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/XKBlib.h>
#include <X11/keysym.h>
#include <X11/Xatom.h>
#include <X11/XF86keysym.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <pwd.h>
#include "settings.h"

#define MAX_CLIENTS_PER_WS 100

static void run_cmd(const char *cmd) {
    if (fork() == 0) {
        setsid();
        execl("/bin/sh", "sh", "-c", cmd, (char *)NULL);
        exit(0);
    }
    while (waitpid(-1, NULL, WNOHANG) > 0);
}

typedef struct {
    Window win;                
    int floating;
    int fullscreen;              
    int x, y, width, height;   
    float split_ratio;         
} Client;

typedef struct {
    Client clients[MAX_CLIENTS_PER_WS]; 
    int count;                          
    int split_vertical;                 
    int stacked_mode;                   
    int stack_index;                    
} Workspace;

static Workspace workspaces[NUM_WORKSPACES]; 
static int current_ws = 0;                   
static Display *dpy;                         
static Window root;                          
static Atom net_desktop_names;
static Atom net_wm_state;
static Atom net_wm_fullscreen;
static Atom net_number_of_desktops;
static Atom net_current_desktop;

static int is_dragging = 0;
static int is_resizing = 0;
static int mouse_start_x, mouse_start_y;
static int win_orig_x, win_orig_y;
static int win_orig_w, win_orig_h;
static Client *drag_client = NULL;

static Client *find_client(Window w) {
    if (w == None || w == root) return NULL;
    
    for (int ws_idx = 0; ws_idx < NUM_WORKSPACES; ws_idx++) {
        Workspace *ws = &workspaces[ws_idx];
        for (int i = 0; i < ws->count; i++) {
            if (ws->clients[i].win == w) return &ws->clients[i];
        }
    }
    
    Window r, p, *children = NULL;
    unsigned int nchildren;
    Window curr = w;
    while (curr != None && curr != root) {
        if (XQueryTree(dpy, curr, &r, &p, &children, &nchildren)) {
            if (children) XFree(children);
            if (p == r || p == None) break;
            
            for (int ws_idx = 0; ws_idx < NUM_WORKSPACES; ws_idx++) {
                Workspace *ws = &workspaces[ws_idx];
                for (int i = 0; i < ws->count; i++) {
                    if (ws->clients[i].win == p) return &ws->clients[i];
                }
            }
            curr = p;
        } else {
            break;
        }
    }
    return NULL;
}

static Client *get_focused_client(void) {
    Window focused;
    int revert;
    XGetInputFocus(dpy, &focused, &revert);
    return find_client(focused);
}

static int is_transient_or_dialog(Window w) {
    Window transient_for = None;
    if (XGetTransientForHint(dpy, w, &transient_for) && transient_for != None)
        return 1;

    Atom actual_type;
    int actual_format;
    unsigned long nitems, bytes_after;
    unsigned char *prop = NULL;

    static Atom net_wm_window_type = None;
    static Atom net_wm_type_dialog = None;
    static Atom net_wm_type_utility = None;
    static Atom net_wm_type_splash = None;

    if (net_wm_window_type == None) {
        net_wm_window_type = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE", False);
        net_wm_type_dialog = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DIALOG", False);
        net_wm_type_utility = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_UTILITY", False);
        net_wm_type_splash  = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_SPLASH", False);
    }

    if (XGetWindowProperty(dpy, w, net_wm_window_type, 0, sizeof(Atom), False,
                           XA_ATOM, &actual_type, &actual_format,
                           &nitems, &bytes_after, &prop) == Success && prop) {
        Atom type = *(Atom *)prop;
        XFree(prop);
        if (type == net_wm_type_dialog || type == net_wm_type_utility || type == net_wm_type_splash)
            return 1;
    }
    return 0;
}

static void restack(void) {
    Client *c = get_focused_client();
    Workspace *ws = &workspaces[current_ws];

    if (c && c->floating) {
        XRaiseWindow(dpy, c->win);
    }

    for (int i = 0; i < ws->count; i++) {
        if (!ws->clients[i].floating) {
            XLowerWindow(dpy, ws->clients[i].win);
        }
    }
    XSync(dpy, False);
}

static void kill_client(Window w) {
    int n, found = 0;
    Atom *protocols;
    Atom wm_delete_window = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
    Atom wm_protocols = XInternAtom(dpy, "WM_PROTOCOLS", False);

    if (XGetWMProtocols(dpy, w, &protocols, &n)) {
        for (int i = 0; i < n; i++) {
            if (protocols[i] == wm_delete_window) {
                found = 1;
                break;
            }
        }
        XFree(protocols);
    }

    if (found) {
        XEvent ev;
        ev.type = ClientMessage;
        ev.xclient.window = w;
        ev.xclient.message_type = wm_protocols;
        ev.xclient.format = 32;
        ev.xclient.data.l[0] = wm_delete_window;
        ev.xclient.data.l[1] = CurrentTime;
        XSendEvent(dpy, w, False, NoEventMask, &ev);
    } else {
        XKillClient(dpy, w);
    }
}

static void update_borders(Window focused_win) {
    Workspace *ws = &workspaces[current_ws];
    for (int i = 0; i < ws->count; i++) {
        if (ws->clients[i].win == focused_win) {
            XSetWindowBorder(dpy, ws->clients[i].win, COLOR_FOCUSED);
        } else {
            XSetWindowBorder(dpy, ws->clients[i].win, COLOR_UNFOCUSED);
        }
    }
}

static void focus_default(void) {
    Workspace *ws = &workspaces[current_ws];
    Window focused;
    int revert;
    XGetInputFocus(dpy, &focused, &revert);
    
    int found = 0;
    for (int i = 0; i < ws->count; i++) {
        if (ws->clients[i].win == focused) {
            found = 1;
            break;
        }
    }
    
    if (!found) {
        if (ws->count > 0) {
            int idx = ws->stacked_mode ? ws->stack_index : 0;
            XSetInputFocus(dpy, ws->clients[idx].win, RevertToParent, CurrentTime);
            update_borders(ws->clients[idx].win);
            restack();
        } else {
            XSetInputFocus(dpy, root, RevertToParent, CurrentTime);
        }
    }
}

static void tile(void) {
    XWindowAttributes root_attr;
    XGetWindowAttributes(dpy, root, &root_attr);
    int sw = root_attr.width;  
    int sh = root_attr.height; 
    Workspace *ws = &workspaces[current_ws];
    
    for (int w = 0; w < NUM_WORKSPACES; w++) {
        if (w == current_ws) continue;
        for (int i = 0; i < workspaces[w].count; i++) {
            XUnmapWindow(dpy, workspaces[w].clients[i].win);
        }
    }

    int has_fullscreen = 0;
    int fs_idx = -1;
    for (int i = 0; i < ws->count; i++) {
        if (ws->clients[i].fullscreen) {
            has_fullscreen = 1;
            fs_idx = i;
            break;
        }
    }

    if (has_fullscreen && fs_idx != -1) {
        
        for (int i = 0; i < ws->count; i++) {
            if (i != fs_idx) XUnmapWindow(dpy, ws->clients[i].win);
        }
 
        XSetWindowBorderWidth(dpy, ws->clients[fs_idx].win, 0);
        XMoveResizeWindow(dpy, ws->clients[fs_idx].win, 0, 0, sw, sh);
        XMapWindow(dpy, ws->clients[fs_idx].win);
        XRaiseWindow(dpy, ws->clients[fs_idx].win);
        return; 
    }

    if (current_ws == SCRATCHPAD_WS) {
        for (int i = 0; i < ws->count; i++) {
            Client *c = &ws->clients[i];
            c->floating = 1;
            XSetWindowBorderWidth(dpy, c->win, BORDER_WIDTH);
            XMoveResizeWindow(dpy, c->win, c->x, c->y, c->width, c->height);
            XMapWindow(dpy, c->win);
        }
        restack();
        return;
    }

    if (ws->stacked_mode) {
        for (int i = 0; i < ws->count; i++) {
            Client *c = &ws->clients[i];
            if (c->floating) continue;

            if (i == ws->stack_index) {
                XSetWindowBorderWidth(dpy, c->win, BORDER_WIDTH);
                XMoveResizeWindow(dpy, c->win, GAP_SIDE, GAP_TOP, 
                                  sw - (GAP_SIDE * 2) - (BORDER_WIDTH * 2), 
                                  sh - GAP_TOP - GAP_SIDE - (BORDER_WIDTH * 2));
                XMapWindow(dpy, c->win);
            } else {
                XUnmapWindow(dpy, c->win);
            }
        }
    } else {
        int tiled_count = 0;
        for (int i = 0; i < ws->count; i++) {
            if (!ws->clients[i].floating) tiled_count++;
        }

        if (tiled_count > 0) {
            int usable_x = GAP_SIDE;
            int usable_y = GAP_TOP;
            int usable_w = sw - (GAP_SIDE * 2);
            int usable_h = sh - GAP_TOP - GAP_SIDE;

            if (tiled_count == 1) {
                for (int i = 0; i < ws->count; i++) {
                    Client *c = &ws->clients[i];
                    if (c->floating) continue;
                    XSetWindowBorderWidth(dpy, c->win, BORDER_WIDTH);
                    XMoveResizeWindow(dpy, c->win, usable_x, usable_y, 
                                      usable_w - (BORDER_WIDTH * 2), 
                                      usable_h - (BORDER_WIDTH * 2));
                    XMapWindow(dpy, c->win);
                }
            } else {
                int master_idx = -1;
                for (int i = 0; i < ws->count; i++) {
                    if (!ws->clients[i].floating) {
                        master_idx = i;
                        break;
                    }
                }
                if (master_idx != -1) {
                    Client *master = &ws->clients[master_idx];
                    if (master->split_ratio <= 0.05f || master->split_ratio >= 0.95f) {
                        master->split_ratio = 0.5f; 
                    }

                    int master_w, master_h, stack_x, stack_y, stack_w, stack_h;
                    if (ws->split_vertical) {
                        int total_h = usable_h - GAP_INNER;
                        master_h = (int)(total_h * master->split_ratio);
                        master_w = usable_w;
                        
                        XSetWindowBorderWidth(dpy, master->win, BORDER_WIDTH);
                        XMoveResizeWindow(dpy, master->win, usable_x, usable_y, 
                                          master_w - (BORDER_WIDTH * 2), master_h - (BORDER_WIDTH * 2));
                        XMapWindow(dpy, master->win);
                        
                        stack_x = usable_x;
                        stack_y = usable_y + master_h + GAP_INNER;
                        stack_w = usable_w;
                        stack_h = usable_h - master_h - GAP_INNER;
                    } else {
                        int total_w = usable_w - GAP_INNER;
                        master_w = (int)(total_w * master->split_ratio);
                        master_h = usable_h;
                        
                        XSetWindowBorderWidth(dpy, master->win, BORDER_WIDTH);
                        XMoveResizeWindow(dpy, master->win, usable_x, usable_y, 
                                          master_w - (BORDER_WIDTH * 2), master_h - (BORDER_WIDTH * 2));
                        XMapWindow(dpy, master->win);
                        
                        stack_x = usable_x + master_w + GAP_INNER;
                        stack_y = usable_y;
                        stack_w = usable_w - master_w - GAP_INNER;
                        stack_h = usable_h;
                    }

                    int stack_count = tiled_count - 1;
                    if (stack_count == 1) {
                        for (int i = 0; i < ws->count; i++) {
                            Client *c = &ws->clients[i];
                            if (c->floating || i == master_idx) continue;
                            XSetWindowBorderWidth(dpy, c->win, BORDER_WIDTH);
                            XMoveResizeWindow(dpy, c->win, stack_x, stack_y, 
                                              stack_w - (BORDER_WIDTH * 2), 
                                              stack_h - (BORDER_WIDTH * 2));
                            XMapWindow(dpy, c->win);
                        }
                    } else if (stack_count >= 2) {
                        int s_indices[MAX_CLIENTS_PER_WS];
                        int s_total = 0;
                        for (int i = 0; i < ws->count; i++) {
                            if (!ws->clients[i].floating && i != master_idx) {
                                s_indices[s_total++] = i;
                            }
                        }
                        
                        if (ws->split_vertical) {
                            int total_inner_gaps = GAP_INNER * (s_total - 1);
                            int sub_w = (stack_w - total_inner_gaps) / s_total;
                            int current_x = stack_x;
                            for (int i = 0; i < s_total; i++) {
                                Client *c = &ws->clients[s_indices[i]];
                                int win_w = (i == s_total - 1) ? (stack_x + stack_w - current_x) : sub_w;
                                XSetWindowBorderWidth(dpy, c->win, BORDER_WIDTH);
                                XMoveResizeWindow(dpy, c->win, current_x, stack_y, 
                                                  win_w - (BORDER_WIDTH * 2), 
                                                  stack_h - (BORDER_WIDTH * 2));
                                XMapWindow(dpy, c->win);
                                current_x += win_w + GAP_INNER;
                            }
                        } else {
                            int total_inner_gaps = GAP_INNER * (s_total - 1);
                            int sub_h = (stack_h - total_inner_gaps) / s_total;
                            int current_y = stack_y;
                            for (int i = 0; i < s_total; i++) {
                                Client *c = &ws->clients[s_indices[i]];
                                int win_h = (i == s_total - 1) ? (stack_y + stack_h - current_y) : sub_h;
                                XSetWindowBorderWidth(dpy, c->win, BORDER_WIDTH);
                                XMoveResizeWindow(dpy, c->win, stack_x, current_y, 
                                                  stack_w - (BORDER_WIDTH * 2), 
                                                  win_h - (BORDER_WIDTH * 2));
                                XMapWindow(dpy, c->win);
                                current_y += win_h + GAP_INNER;
                            }
                        }
                    }
                }
            }
        }
    }

    for (int i = 0; i < ws->count; i++) {
        Client *c = &ws->clients[i];
        if (c->floating) {
            XSetWindowBorderWidth(dpy, c->win, BORDER_WIDTH);
            XMoveResizeWindow(dpy, c->win, c->x, c->y, c->width, c->height);
            XMapWindow(dpy, c->win);
        }
    }
    restack();
}

static int add_client_to_ws(Window w, int target_ws) {
    Workspace *ws = &workspaces[target_ws];
    if (ws->count >= MAX_CLIENTS_PER_WS) return 0;
    
    for (int i = 0; i < ws->count; i++) {
        if (ws->clients[i].win == w) return 0;
    }
    
    XWindowAttributes attr;
    XGetWindowAttributes(dpy, w, &attr);
    
    ws->clients[ws->count].win = w;
    ws->clients[ws->count].split_ratio = 0.5f;
    ws->clients[ws->count].fullscreen = 0;
    
    int float_by_default = (target_ws == SCRATCHPAD_WS) || is_transient_or_dialog(w);
    ws->clients[ws->count].floating = float_by_default;
    ws->clients[ws->count].x = (target_ws == SCRATCHPAD_WS || float_by_default) ? (attr.x > 0 ? attr.x : 200) : attr.x;
    ws->clients[ws->count].y = (target_ws == SCRATCHPAD_WS || float_by_default) ? (attr.y > 0 ? attr.y : 150) : attr.y;
    ws->clients[ws->count].width = attr.width > 50 ? attr.width : 800;
    ws->clients[ws->count].height = attr.height > 50 ? attr.height : 600;
    
    ws->count++;
    
    if (ws->stacked_mode) {
        ws->stack_index = ws->count - 1;
    }
    
    XSetWindowBorderWidth(dpy, w, BORDER_WIDTH);
    XSelectInput(dpy, w, EnterWindowMask | FocusChangeMask | PropertyChangeMask);
    XGrabButton(dpy, Button1, MODKEY, w, True, ButtonPressMask | ButtonReleaseMask | PointerMotionMask, GrabModeAsync, GrabModeAsync, None, None);
    XGrabButton(dpy, Button3, MODKEY, w, True, ButtonPressMask | ButtonReleaseMask | PointerMotionMask, GrabModeAsync, GrabModeAsync, None, None);
    
    return 1;
}

static void remove_client_from_ws(Window w, int ws_index) {
    Workspace *ws = &workspaces[ws_index];
    int found = -1;
    for (int i = 0; i < ws->count; i++) {
        if (ws->clients[i].win == w) {
            found = i;
            break;
        }
    }
    
    if (found != -1) {
        for (int i = found; i < ws->count - 1; i++) {
            ws->clients[i] = ws->clients[i + 1];
        }
        ws->count--;
        memset(&ws->clients[ws->count], 0, sizeof(Client));
        
        if (ws->stack_index >= ws->count && ws->count > 0) {
            ws->stack_index = ws->count - 1;
        } else if (ws->count == 0) {
            ws->stack_index = 0;
        }
    }
}

static void remove_client(Window w) {
    Window focused;
    int revert;
    XGetInputFocus(dpy, &focused, &revert);
    int was_focused = (focused == w);
    
    for (int ws_idx = 0; ws_idx < NUM_WORKSPACES; ws_idx++) {
        remove_client_from_ws(w, ws_idx);
    }
    
    if (was_focused) {
        focus_default();
    }
}

static void move_window_to_workspace(int target_ws) {
    if (target_ws < 0 || target_ws >= NUM_WORKSPACES || target_ws == current_ws)
        return;
        
    Client *c = get_focused_client();
    if (!c) return;
    
    Window focused = c->win;
    Client target_client = *c;
    
    remove_client_from_ws(focused, current_ws);
    XUnmapWindow(dpy, focused);
    
    Workspace *dst_ws = &workspaces[target_ws];
    if (dst_ws->count < MAX_CLIENTS_PER_WS) {
        if (target_ws == SCRATCHPAD_WS) {
            target_client.floating = 1;
        }
        dst_ws->clients[dst_ws->count] = target_client;
        dst_ws->count++;
        if (dst_ws->stacked_mode) {
            dst_ws->stack_index = dst_ws->count - 1;
        }
    }
    
    tile();
    focus_default();
}

static void toggle_float_single(void) {
    Workspace *ws = &workspaces[current_ws];
    if (current_ws == SCRATCHPAD_WS || ws->stacked_mode) return;
    
    Client *c = get_focused_client();
    if (!c) return;
    
    c->floating = !c->floating;
    if (c->floating) {
        XWindowAttributes root_attr;
        XGetWindowAttributes(dpy, root, &root_attr);
        int sw = root_attr.width;
        int sh = root_attr.height;
        c->width = sw / 2;
        c->height = sh / 2;
        c->x = (sw - c->width) / 2;
        c->y = (sh - c->height) / 2;
    }
    tile();
}

static void toggle_float_all(void) {
    Workspace *ws = &workspaces[current_ws];
    for (int i = 0; i < ws->count; i++) {
        Client *c = &ws->clients[i];
        c->floating = !c->floating;
        if (c->floating) {
            XWindowAttributes attr;
            if (XGetWindowAttributes(dpy, c->win, &attr)) {
                c->x = attr.x;
                c->y = attr.y;
                c->width = attr.width;
                c->height = attr.height;
            }
        }
    }
    tile();
}

static void focus_change(int dir) {
    Workspace *ws = &workspaces[current_ws];
    if (ws->count <= 1) return;
    
    Client *cur = get_focused_client();
    int cur_idx = 0;
    if (cur) {
        for (int i = 0; i < ws->count; i++) {
            if (&ws->clients[i] == cur) {
                cur_idx = i;
                break;
            }
        }
    }
    
    int next_idx = (cur_idx + dir + ws->count) % ws->count;
    Window target = ws->clients[next_idx].win;
    
    XSetInputFocus(dpy, target, RevertToParent, CurrentTime);
    update_borders(target);
    restack();
}

static void view_workspace(int target_ws) {
    if (target_ws < 0 || target_ws >= NUM_WORKSPACES || target_ws == current_ws)
        return;
        
    for (int i = 0; i < workspaces[current_ws].count; i++) {
        XUnmapWindow(dpy, workspaces[current_ws].clients[i].win);
    }
    
    current_ws = target_ws;
    XChangeProperty(dpy, root, XInternAtom(dpy, "_NET_CURRENT_DESKTOP", False), XA_CARDINAL, 32, PropModeReplace, (unsigned char *)&current_ws, 1);
    tile();
    
    Workspace *ws = &workspaces[current_ws];
    if (ws->count > 0) {
        int focus_idx = ws->stacked_mode ? ws->stack_index : 0;
        XSetInputFocus(dpy, ws->clients[focus_idx].win, RevertToParent, CurrentTime);
        update_borders(ws->clients[focus_idx].win);
        restack();
    } else {
        XSetInputFocus(dpy, root, RevertToParent, CurrentTime);
    }
}

int on_x_error(Display *d, XErrorEvent *e) {
    (void)d; (void)e;
    return 0;
}

int main(void) {
    dpy = XOpenDisplay(NULL);
    if (!dpy) {
        fprintf(stderr, "sehwm: Failed to open X display!\n");
        return 1;
    }
    
    XSetErrorHandler(on_x_error);
    root = DefaultRootWindow(dpy);
    
    Atom net_number_of_desktops = XInternAtom(dpy, "_NET_NUMBER_OF_DESKTOPS", False);
    Atom net_current_desktop = XInternAtom(dpy, "_NET_CURRENT_DESKTOP", False);
    net_desktop_names = XInternAtom(dpy, "_NET_DESKTOP_NAMES", False);
    net_wm_state = XInternAtom(dpy, "_NET_WM_STATE", False);
    net_wm_fullscreen = XInternAtom(dpy, "_NET_WM_STATE_FULLSCREEN", False);
    
    long num_workspaces = NUM_WORKSPACES;
    long current_desktop = current_ws;
    
    XChangeProperty(dpy, root, net_number_of_desktops, XA_CARDINAL, 32, PropModeReplace, (unsigned char *)&num_workspaces, 1);
    XChangeProperty(dpy, root, net_current_desktop, XA_CARDINAL, 32, PropModeReplace, (unsigned char *)&current_desktop, 1);
    
    unsigned char names[] = {'1', '\0', '2', '\0', '3', '\0', '4', '\0', '5', '\0', '6', '\0', '7', '\0', '8', '\0', '9', '\0', '1', '0', '\0'};
    XChangeProperty(dpy, root, net_desktop_names, XInternAtom(dpy, "UTF8_STRING", False), 8, PropModeReplace, names, sizeof(names));
    
    
    Atom net_supported = XInternAtom(dpy, "_NET_SUPPORTED", False);
    Atom supported_atoms[] = { net_wm_state, net_wm_fullscreen };
    XChangeProperty(dpy, root, net_supported, XA_ATOM, 32, PropModeReplace, (unsigned char *)supported_atoms, 2);
    
    XSelectInput(dpy, root, SubstructureRedirectMask | SubstructureNotifyMask | ButtonPressMask);
    
    for (int i = 0; i < NUM_WORKSPACES; i++) {
        workspaces[i].split_vertical = 0;
    }
    
    KeyCode ws_codes[NUM_WORKSPACES];
    KeySym ws_keysyms[NUM_WORKSPACES] = {XK_1, XK_2, XK_3, XK_4, XK_5, XK_6, XK_7, XK_8, XK_9, XK_0};
    
    for (int i = 0; i < NUM_WORKSPACES; i++) {
        ws_codes[i] = XKeysymToKeycode(dpy, ws_keysyms[i]);
    }
    
    unsigned int modifiers[] = { 0, LockMask, Mod2Mask, LockMask | Mod2Mask };
    unsigned int shift_modifiers[] = { ShiftMask, LockMask | ShiftMask, Mod2Mask | ShiftMask, LockMask | Mod2Mask | ShiftMask };
    unsigned int num_keys = sizeof(keys) / sizeof(keys[0]);
    
    for (unsigned int i = 0; i < num_keys; i++) {
        KeyCode code = XKeysymToKeycode(dpy, keys[i].keysym);
        if (code == 0) continue;
        for (int m = 0; m < 4; m++) {
            XGrabKey(dpy, code, keys[i].mod | modifiers[m], root, True, GrabModeAsync, GrabModeAsync);
        }
    }
    
    for (int i = 0; i < 4; i++) {
        for (int w = 0; w < NUM_WORKSPACES; w++) {
            XGrabKey(dpy, ws_codes[w], MODKEY | modifiers[i], root, True, GrabModeAsync, GrabModeAsync);
            XGrabKey(dpy, ws_codes[w], MODKEY | shift_modifiers[i], root, True, GrabModeAsync, GrabModeAsync);
        }
    }
    
    XSync(dpy, False);
    printf("sehwm running successfully\n");
    
    XEvent ev;
    while (1) {
        XNextEvent(dpy, &ev);
        switch (ev.type) {
            case ClientMessage: {
                if (ev.xclient.message_type == net_wm_state && ev.xclient.format == 32) {
                    Window w = ev.xclient.window;
                    Client *c = find_client(w);
                    if (c) {
                        Atom action = ev.xclient.data.l[0];
                        Atom prop = ev.xclient.data.l[1];
                        if (prop == net_wm_fullscreen) {
                            if (action == 1) c->fullscreen = !c->fullscreen;
                            else if (action == 2) c->fullscreen = 1;
                            else if (action == 0) c->fullscreen = 0;
                            tile();
                        }
                    }
                }
                break;
            }
            case MapRequest: {
                Window w = ev.xmap.window;
                XWindowAttributes attr;
                if (XGetWindowAttributes(dpy, w, &attr) && attr.override_redirect) {
                    XMapWindow(dpy, w);
                    break;
                }
                if (add_client_to_ws(w, current_ws)) {
                    XMapWindow(dpy, w);
                    XSetInputFocus(dpy, w, RevertToParent, CurrentTime);
                    update_borders(w);
                    tile();
                }
                break;
            }
            case UnmapNotify: {
                Client *c = find_client(ev.xunmap.window);
                if (c) {
                    XWindowAttributes attr;
                    if (XGetWindowAttributes(dpy, ev.xunmap.window, &attr) && attr.map_state == IsUnmapped) {
                        if (ev.xunmap.send_event) {
                            remove_client(ev.xunmap.window);
                            tile();
                        }
                    }
                }
                break;
            }
            case DestroyNotify:
                remove_client(ev.xdestroywindow.window);
                tile();
                break;
            case EnterNotify: {
                Window w = ev.xcrossing.window;
                if (w != root) {
                    XSetInputFocus(dpy, w, RevertToParent, CurrentTime);
                    update_borders(w);
                    restack();
                }
                break;
            }
            case ButtonPress: {
                Window w = ev.xbutton.subwindow;
                if (w == None) w = ev.xbutton.window;
                
                Client *c = find_client(w);
                if (c && c->floating) {
                    XSetInputFocus(dpy, c->win, RevertToParent, CurrentTime);
                    update_borders(c->win);
                    restack();
                    if (ev.xbutton.button == Button1 && (ev.xbutton.state & MODKEY)) {
                        is_dragging = 1;
                        is_resizing = 0;
                        drag_client = c;
                        mouse_start_x = ev.xbutton.x_root;
                        mouse_start_y = ev.xbutton.y_root;
                        win_orig_x = c->x;
                        win_orig_y = c->y;
                        XGrabPointer(dpy, root, True, PointerMotionMask | ButtonReleaseMask, GrabModeAsync, GrabModeAsync, None, None, CurrentTime);
                    }
                    else if (ev.xbutton.button == Button3 && (ev.xbutton.state & MODKEY)) {
                        is_resizing = 1;
                        is_dragging = 0;
                        drag_client = c;
                        mouse_start_x = ev.xbutton.x_root;
                        mouse_start_y = ev.xbutton.y_root;
                        win_orig_w = c->width;
                        win_orig_h = c->height;
                        XGrabPointer(dpy, root, True, PointerMotionMask | ButtonReleaseMask, GrabModeAsync, GrabModeAsync, None, None, CurrentTime);
                    }
                }
                break;
            }
            case MotionNotify: {
                if (drag_client && drag_client->floating) {
                    while (XCheckTypedEvent(dpy, MotionNotify, &ev));
                    int dx = ev.xmotion.x_root - mouse_start_x;
                    int dy = ev.xmotion.y_root - mouse_start_y;
                    if (is_dragging) {
                        drag_client->x = win_orig_x + dx;
                        drag_client->y = win_orig_y + dy;
                    } else if (is_resizing) {
                        drag_client->width = (win_orig_w + dx > 50) ? win_orig_w + dx : 50;
                        drag_client->height = (win_orig_h + dy > 50) ? win_orig_h + dy : 50;
                    }
                    XMoveResizeWindow(dpy, drag_client->win, drag_client->x, drag_client->y, drag_client->width, drag_client->height);
                    XSync(dpy, False);
                }
                break;
            }
            case ButtonRelease: {
                if (is_dragging || is_resizing) {
                    XUngrabPointer(dpy, CurrentTime);
                    is_dragging = 0;
                    is_resizing = 0;
                    drag_client = NULL;
                    tile();
                }
                break;
            }
            case KeyPress: {
                int matched = 0;
                for (unsigned int i = 0; i < num_keys; i++) {
                    if (XKeysymToKeycode(dpy, keys[i].keysym) == ev.xkey.keycode &&
                        (ev.xkey.state & ~(LockMask | Mod2Mask)) == (keys[i].mod & ~(LockMask | Mod2Mask))) {
                        
                        matched = 1;
                        Workspace *ws = &workspaces[current_ws];
                        
                        if (keys[i].type == CMD_SPAWN) {
                            run_cmd(keys[i].arg);
                        } else if (keys[i].type == FUNC_KILL) {
                            Client *c = get_focused_client();
                            if (c) kill_client(c->win);
                        } else if (keys[i].type == FUNC_FOCUS) {
                            focus_change(keys[i].int_arg);
                        } else if (keys[i].type == FUNC_FLOAT_SINGLE) {
                            toggle_float_single();
                        } else if (keys[i].type == FUNC_FLOAT_ALL) {
                            toggle_float_all();
                        } else if (keys[i].type == FUNC_SPLIT) {
                            ws->split_vertical = !ws->split_vertical;
                            tile();
                        } else if (keys[i].type == FUNC_STACK_TOGGLE) {
                            ws->stacked_mode = !ws->stacked_mode;
                            if (ws->stacked_mode && ws->count > 0) ws->stack_index = 0;
                            tile();
                            if (ws->stacked_mode && ws->count > 0) {
                                update_borders(ws->clients[ws->stack_index].win);
                                restack();
                            }
                        } else if (keys[i].type == FUNC_STACK_CYCLE) {
                            if (ws->stacked_mode && ws->count > 0) {
                                ws->stack_index = (ws->stack_index + keys[i].int_arg + ws->count) % ws->count;
                                tile();
                                update_borders(ws->clients[ws->stack_index].win);
                                restack();
                            }
                        } else if (keys[i].type == FUNC_DIR_RESIZE) {
                            int is_vertical_key = (keys[i].keysym == XK_Up || keys[i].keysym == XK_Down);
                            if (ws->split_vertical == is_vertical_key) {
                                int master_idx = -1;
                                for (int c_idx = 0; c_idx < ws->count; c_idx++) {
                                    if (!ws->clients[c_idx].floating) { 
                                        master_idx = c_idx; 
                                        break; 
                                    }
                                }
                                if (master_idx != -1) {
                                    Client *master = &ws->clients[master_idx];
                                    float step = 0.05f;
                                    master->split_ratio += (keys[i].int_arg > 0) ? step : -step;
                                    if (master->split_ratio > 0.9f) master->split_ratio = 0.9f;
                                    if (master->split_ratio < 0.1f) master->split_ratio = 0.1f;
                                    tile();
                                }
                            }
                        } else if (keys[i].type == FUNC_SWAP) {
                            Client *c = get_focused_client();
                            if (c && ws->count > 1) {
                                int f_idx = -1;
                                for (int idx = 0; idx < ws->count; idx++) {
                                    if (&ws->clients[idx] == c) { 
                                        f_idx = idx; 
                                        break; 
                                    }
                                }
                                if (f_idx > 0) {
                                    Client temp = ws->clients[0];
                                    ws->clients[0] = ws->clients[f_idx];
                                    ws->clients[f_idx] = temp;
                                    tile();
                                    update_borders(ws->clients[0].win);
                                    restack();
                                }
                            }
                        }
                        break;
                    }
                }
                if (!matched) {
                    for (int w = 0; w < NUM_WORKSPACES; w++) {
                        if (ev.xkey.keycode == ws_codes[w]) {
                            if (ev.xkey.state & ShiftMask) move_window_to_workspace(w);
                            else view_workspace(w);
                            break;
                        }
                    }
                }
                break;
            }
            default:
                break;
        }
    }
    
    XCloseDisplay(dpy);
    return 0;
}
