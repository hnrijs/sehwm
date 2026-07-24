#include <X11/Xlib.h>
#include <X11/XKBlib.h>
#include <X11/keysym.h>
#include <X11/XF86keysym.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <pwd.h>

#define MAX_CLIENTS_PER_WS 100   

static void run_cmd(const char *cmd) {
    if (fork() == 0) {
        setsid();
        execl("/bin/sh", "sh", "-c", cmd, (char *)NULL);
        exit(0);
    }
    while (waitpid(-1, NULL, WNOHANG) > 0);
}

#include "settings.h"

typedef struct {
    Window win;                
    int floating;              
    int x, y, width, height;   
    float split_ratio;         
    float stack_ratio;         
    float secondary_ratio;    
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

static void update_borders(Window focused_win) {
    for (int i = 0; i < workspaces[current_ws].count; i++) {
        if (workspaces[current_ws].clients[i].win == focused_win) {
            XSetWindowBorder(dpy, workspaces[current_ws].clients[i].win, COLOR_FOCUSED);
            XRaiseWindow(dpy, workspaces[current_ws].clients[i].win);
        } else {
            XSetWindowBorder(dpy, workspaces[current_ws].clients[i].win, COLOR_UNFOCUSED);
        }
    }
}

static void tile(void) {
    XWindowAttributes root_attr;
    XGetWindowAttributes(dpy, root, &root_attr);
    int sw = root_attr.width;  
    int sh = root_attr.height; 
    Workspace *ws = &workspaces[current_ws];
    
    if (current_ws == SCRATCHPAD_WS) {
        for (int i = 0; i < ws->count; i++) {
            Client *c = &ws->clients[i];
            c->floating = 1;
            XMoveResizeWindow(dpy, c->win, c->x, c->y, c->width, c->height);
            XMapWindow(dpy, c->win);
        }
        return;
    }
    
    if (ws->stacked_mode) {
        for (int i = 0; i < ws->count; i++) {
            Client *c = &ws->clients[i];
            if (c->floating) continue;

            if (i == ws->stack_index) {
                XMoveResizeWindow(dpy, c->win, GAP_SIDE, GAP_TOP, 
                                  sw - (GAP_SIDE * 2) - (BORDER_WIDTH * 2), 
                                  sh - GAP_TOP - GAP_SIDE - (BORDER_WIDTH * 2));
                XMapWindow(dpy, c->win);
                XRaiseWindow(dpy, c->win);
            } else {
                XMoveWindow(dpy, c->win, -3000, -3000);
            }
        }
        return;
    }

    int tiled_count = 0;
    for (int i = 0; i < ws->count; i++) {
        if (!ws->clients[i].floating) tiled_count++;
    }
    if (tiled_count <= 0) return;

    int usable_x = GAP_SIDE;
    int usable_y = GAP_TOP;
    int usable_w = sw - (GAP_SIDE * 2);
    int usable_h = sh - GAP_TOP - GAP_SIDE;

    if (tiled_count == 1) {
        for (int i = 0; i < ws->count; i++) {
            Client *c = &ws->clients[i];
            if (c->floating) continue;
            XMoveResizeWindow(dpy, c->win, usable_x, usable_y, 
                              usable_w - (BORDER_WIDTH * 2), 
                              usable_h - (BORDER_WIDTH * 2));
            XMapWindow(dpy, c->win);
        }
        return;
    }

    int master_idx = -1;
    for (int i = 0; i < ws->count; i++) {
        if (!ws->clients[i].floating) {
            master_idx = i;
            break;
        }
    }
    if (master_idx == -1) return;
    
    Client *master = &ws->clients[master_idx];
    if (master->split_ratio <= 0.0f || master->split_ratio >= 1.0f) {
        master->split_ratio = 0.5f; 
    }

    int master_w, master_h, stack_x, stack_y, stack_w, stack_h;
    if (ws->split_vertical) {
        int total_h = usable_h - GAP_INNER;
        master_h = (int)(total_h * master->split_ratio);
        master_w = usable_w;
        
        XMoveResizeWindow(dpy, master->win, usable_x, usable_y, 
                          master_w - (BORDER_WIDTH * 2), 
                          master_h - (BORDER_WIDTH * 2));
        XMapWindow(dpy, master->win);
        stack_x = usable_x;
        stack_y = usable_y + master_h + GAP_INNER;
        stack_w = usable_w;
        stack_h = usable_h - master_h - GAP_INNER;
    } else {
        int total_w = usable_w - GAP_INNER;
        master_w = (int)(total_w * master->split_ratio);
        master_h = usable_h;  
        XMoveResizeWindow(dpy, master->win, usable_x, usable_y, 
                          master_w - (BORDER_WIDTH * 2), 
                          master_h - (BORDER_WIDTH * 2));
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
                
                XMoveResizeWindow(dpy, c->win, stack_x, current_y, 
                                  stack_w - (BORDER_WIDTH * 2), 
                                  win_h - (BORDER_WIDTH * 2));
                XMapWindow(dpy, c->win);
                current_y += win_h + GAP_INNER;
            }
        }
    }
}

static int add_client_to_ws(Window w, int target_ws) {
    Workspace *ws = &workspaces[target_ws];
    for (int i = 0; i < ws->count; i++) {
        if (ws->clients[i].win == w) return 0; 
    }
    if (ws->count < MAX_CLIENTS_PER_WS) {
        XWindowAttributes attr;
        XGetWindowAttributes(dpy, w, &attr);   
        ws->clients[ws->count].win = w;
        ws->clients[ws->count].split_ratio = 0.5f;
        ws->clients[ws->count].stack_ratio = 0.33f;
        ws->clients[ws->count].secondary_ratio = 0.33f;
        ws->clients[ws->count].floating = (target_ws == SCRATCHPAD_WS) ? 1 : 0;
        ws->clients[ws->count].x = (target_ws == SCRATCHPAD_WS) ? 100 : attr.x;
        ws->clients[ws->count].y = (target_ws == SCRATCHPAD_WS) ? 100 : attr.y;
        ws->clients[ws->count].width = attr.width > 50 ? attr.width : 800;
        ws->clients[ws->count].height = attr.height > 50 ? attr.height : 600;
        ws->count++;
        if (ws->stacked_mode) {
            ws->stack_index = ws->count - 1;
        }    
        XSetWindowBorderWidth(dpy, w, BORDER_WIDTH);
        XSelectInput(dpy, w, EnterWindowMask | FocusChangeMask | PropertyChangeMask);
        return 1;
    }
    return 0;
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
        if (ws->stack_index >= ws->count && ws->count > 0) {
            ws->stack_index = ws->count - 1;
        }
    }
}

static void remove_client(Window w) {
    for (int ws_idx = 0; ws_idx < NUM_WORKSPACES; ws_idx++) {
        remove_client_from_ws(w, ws_idx);
    }
}

static void move_window_to_workspace(int target_ws) {
    if (target_ws < 0 || target_ws >= NUM_WORKSPACES || target_ws == current_ws)
        return;
    Window focused;
    int revert;
    XGetInputFocus(dpy, &focused, &revert);
    if (focused == root || focused == None) return;
    
    Client target_client;
    int found = 0;
    Workspace *cur_ws = &workspaces[current_ws];
    for (int i = 0; i < cur_ws->count; i++) {
        if (cur_ws->clients[i].win == focused) {
            target_client = cur_ws->clients[i];
            found = 1;
            break;
        }
    }
    if (!found) return;
    remove_client_from_ws(focused, current_ws);
    XMoveWindow(dpy, focused, -3000, -3000);
    tile();
    
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
}

static void toggle_float_single(void) {
    Workspace *ws = &workspaces[current_ws];
    if (current_ws == SCRATCHPAD_WS || ws->stacked_mode) return;

    Window focused;
    int revert;
    XGetInputFocus(dpy, &focused, &revert);
    if (focused == root || focused == None) return;

    for (int i = 0; i < ws->count; i++) {
        Client *c = &ws->clients[i];
        if (c->win == focused) {
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
                XMoveResizeWindow(dpy, c->win, c->x, c->y, c->width, c->height);
                XRaiseWindow(dpy, c->win);
            }
            tile();
            break;
        }
    }
}

static void toggle_float_all(void) {
    Workspace *ws = &workspaces[current_ws];
    for (int i = 0; i < ws->count; i++) {
        ws->clients[i].floating = !ws->clients[i].floating;
    }
    tile();
}

static void view_workspace(int target_ws) {
    if (target_ws < 0 || target_ws >= NUM_WORKSPACES || target_ws == current_ws)
        return;

    for (int i = 0; i < workspaces[current_ws].count; i++) {
        XMoveWindow(dpy, workspaces[current_ws].clients[i].win, -3000, -3000);
    }
    current_ws = target_ws; 
    tile(); 

    Workspace *ws = &workspaces[current_ws];
    if (ws->count > 0) {
        int focus_idx = ws->stacked_mode ? ws->stack_index : 0;
        XSetInputFocus(dpy, ws->clients[focus_idx].win, RevertToParent, CurrentTime);
        update_borders(ws->clients[focus_idx].win);
    }
}

int on_x_error(Display *d, XErrorEvent *e) {
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
    
    XSelectInput(dpy, root, SubstructureRedirectMask | SubstructureNotifyMask | ButtonPressMask);

    for (int i = 0; i < NUM_WORKSPACES; i++) {
        workspaces[i].split_vertical = 0; 
    }    

    KeyCode ws_codes[NUM_WORKSPACES];
    KeySym ws_keysyms[NUM_WORKSPACES] = { 
        XK_1, XK_2, XK_3, XK_4, XK_5, 
        XK_6, XK_7, XK_8, XK_9, XK_0 
    };
    for (int i = 0; i < NUM_WORKSPACES; i++) {
        ws_codes[i] = XKeysymToKeycode(dpy, ws_keysyms[i]);
    }

    unsigned int modifiers[] = { 0, LockMask, Mod2Mask, LockMask | Mod2Mask };
    unsigned int shift_modifiers[] = { ShiftMask, LockMask | ShiftMask, Mod2Mask | ShiftMask, LockMask | Mod2Mask | ShiftMask };
          
   
    for (unsigned int i = 0; i < (sizeof(keys) / sizeof(keys[0])); i++) {
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
            case MapRequest: {
                Window w = ev.xmap.window;
                if (add_client_to_ws(w, current_ws)) {
                    XMapWindow(dpy, w);
                    XSetInputFocus(dpy, w, RevertToParent, CurrentTime);
                    update_borders(w);
                    tile();
                }
                break;
            }
            case UnmapNotify:
                remove_client(ev.xunmap.window);
                tile();
                break;
            case DestroyNotify:
                remove_client(ev.xdestroywindow.window);
                tile();
                break;
            case EnterNotify: {
                Window w = ev.xcrossing.window;
                if (w != root) {
                    XSetInputFocus(dpy, w, RevertToParent, CurrentTime);
                    update_borders(w);
                }
                break;
            }
            case KeyPress: {
                int matched = 0;
                for (unsigned int i = 0; i < (sizeof(keys) / sizeof(keys[0])); i++) {
                    if (XKeysymToKeycode(dpy, keys[i].keysym) == ev.xkey.keycode &&
                        (ev.xkey.state & ~(LockMask | Mod2Mask)) == (keys[i].mod & ~(LockMask | Mod2Mask))) {
                        
                        matched = 1;
                        Workspace *ws = &workspaces[current_ws];

                        if (keys[i].type == CMD_SPAWN) {
                            spawn_cmd(keys[i].arg);
                        } else if (keys[i].type == FUNC_KILL) {
                            Window focused;
                            int revert;
                            XGetInputFocus(dpy, &focused, &revert);
                            if (focused != root && focused != None) {
                                XKillClient(dpy, focused);
                            }
                        } else if (keys[i].type == FUNC_FLOAT_SINGLE) {
                            toggle_float_single();
                        } else if (keys[i].type == FUNC_FLOAT_ALL) {
                            toggle_float_all();
                        } else if (keys[i].type == FUNC_SPLIT) {
                            ws->split_vertical = !ws->split_vertical;
                            tile();
                        } else if (keys[i].type == FUNC_STACK_TOGGLE) {
                            ws->stacked_mode = !ws->stacked_mode;
                            if (ws->stacked_mode && ws->count > 0) {
                                ws->stack_index = 0;
                            }
                            tile();
                            if (ws->stacked_mode && ws->count > 0) {
                                update_borders(ws->clients[ws->stack_index].win);
                            }
                        } else if (keys[i].type == FUNC_STACK_CYCLE) {
                            if (ws->stacked_mode && ws->count > 0) {
                                ws->stack_index = (ws->stack_index + keys[i].int_arg + ws->count) % ws->count;
                                tile();
                                update_borders(ws->clients[ws->stack_index].win);
                            }
                        } else if (keys[i].type == FUNC_SWAP) {
                            Window focused;
                            int revert;
                            XGetInputFocus(dpy, &focused, &revert);
                            if (focused != root && focused != None && ws->count > 1) {
                                int f_idx = -1;
                                for (int c = 0; c < ws->count; c++) {
                                    if (ws->clients[c].win == focused) {
                                        f_idx = c;
                                        break;
                                    }
                                }
                                if (f_idx > 0) {
                                    Client temp = ws->clients[0];
                                    ws->clients[0] = ws->clients[f_idx];
                                    ws->clients[f_idx] = temp;
                                    tile();
                                    update_borders(ws->clients[0].win);
                                }
                            }
                        } else if (keys[i].type == FUNC_DIR_SWAP) {
                          
                            tile();
                        }
                        break;
                    }
                }

                if (!matched) {
                    for (int w = 0; w < NUM_WORKSPACES; w++) {
                        if (ev.xkey.keycode == ws_codes[w]) {
                            if (ev.xkey.state & ShiftMask) {
                                move_window_to_workspace(w);
                            } else {
                                view_workspace(w);
                            }
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
