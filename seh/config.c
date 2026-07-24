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

#define MODKEY Mod4Mask          
#define MAX_CLIENTS_PER_WS 100   
#define NUM_WORKSPACES 10        
#define SCRATCHPAD_WS 9          
#define BORDER_WIDTH 2           
#define COLOR_FOCUSED 0xFFFFFF   
#define COLOR_UNFOCUSED 0x000000 
#define GAP_INNER 5              
#define GAP_TOP 5                
#define GAP_SIDE 5              

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


static void toggle_float_all(void) {
    Workspace *ws = &workspaces[current_ws];
    if (current_ws == SCRATCHPAD_WS || ws->stacked_mode) return;
    if (ws->count == 0) return;

    int any_tiled = 0;
    for (int i = 0; i < ws->count; i++) {
        if (!ws->clients[i].floating) {
            any_tiled = 1;
            break;
        }
    }

    for (int i = 0; i < ws->count; i++) {
        ws->clients[i].floating = any_tiled;
        if (any_tiled) {
            XWindowAttributes attr;
            XGetWindowAttributes(dpy, ws->clients[i].win, &attr);
            ws->clients[i].x = attr.x;
            ws->clients[i].y = attr.y;
            ws->clients[i].width = attr.width;
            ws->clients[i].height = attr.height;
            XRaiseWindow(dpy, ws->clients[i].win);
        }
    }
    if (!any_tiled) {
        tile();
    }
}


static void toggle_stacked(void) {
    Workspace *ws = &workspaces[current_ws];
    if (current_ws == SCRATCHPAD_WS) return;

    ws->stacked_mode = !ws->stacked_mode;
    ws->stack_index = ws->count > 0 ? ws->count - 1 : 0;
    tile();
    if (ws->count > 0 && ws->stacked_mode) {
        XSetInputFocus(dpy, ws->clients[ws->stack_index].win, RevertToParent, CurrentTime);
        update_borders(ws->clients[ws->stack_index].win);
    }
}


static void cycle_stack(int direction) {
    Workspace *ws = &workspaces[current_ws];
    if (!ws->stacked_mode || ws->count <= 1) return;

    ws->stack_index = (ws->stack_index + direction + ws->count) % ws->count;
    tile();
    XSetInputFocus(dpy, ws->clients[ws->stack_index].win, RevertToParent, CurrentTime);
    update_borders(ws->clients[ws->stack_index].win);
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


static void swap_windows(void) {
    Workspace *ws = &workspaces[current_ws];
    if (ws->count < 2 || current_ws == SCRATCHPAD_WS || ws->stacked_mode) return;

    Window focused;
    int revert;
    XGetInputFocus(dpy, &focused, &revert);

    int focused_idx = -1;
    for (int i = 0; i < ws->count; i++) {
        if (ws->clients[i].win == focused) {
            focused_idx = i;
            break;
        }
    }

    if (focused_idx != -1) {
        int target_idx = (focused_idx == 0) ? 1 : 0;
        float old_master_ratio = ws->clients[0].split_ratio;
        
        Client temp = ws->clients[focused_idx];
        ws->clients[focused_idx] = ws->clients[target_idx];
        ws->clients[target_idx] = temp;

        ws->clients[0].split_ratio = old_master_ratio;
        ws->clients[target_idx].secondary_ratio = 0.33f;

        tile();
        XSetInputFocus(dpy, ws->clients[target_idx].win, RevertToParent, CurrentTime);
        update_borders(ws->clients[target_idx].win);
    }
}


static void swap_windows_direction(int direction) {
    Workspace *ws = &workspaces[current_ws];
    if (ws->count < 2 || current_ws == SCRATCHPAD_WS || ws->stacked_mode) return;

    Window focused;
    int revert;
    XGetInputFocus(dpy, &focused, &revert);

    int focused_idx = -1;
    for (int i = 0; i < ws->count; i++) {
        if (ws->clients[i].win == focused) {
            focused_idx = i;
            break;
        }
    }

    if (focused_idx == -1) return;

    int target_idx = focused_idx;
    if (direction == 0 && focused_idx > 0) {
        target_idx = focused_idx - 1;
    } else if (direction == 1 && focused_idx < ws->count - 1) {
        target_idx = focused_idx + 1;
    }

    if (target_idx != focused_idx) {
        Client temp = ws->clients[focused_idx];
        ws->clients[focused_idx] = ws->clients[target_idx];
        ws->clients[target_idx] = temp;
        tile();
        XSetInputFocus(dpy, ws->clients[target_idx].win, RevertToParent, CurrentTime);
        update_borders(ws->clients[target_idx].win);
    }
}


static void resize_window_direction(int keycode) {
    Workspace *ws = &workspaces[current_ws];
    if (current_ws == SCRATCHPAD_WS || ws->stacked_mode || ws->count == 0) return;

    Window focused;
    int revert;
    XGetInputFocus(dpy, &focused, &revert);
    if (focused == root || focused == None) return;

    int client_idx = -1;
    for (int i = 0; i < ws->count; i++) {
        if (ws->clients[i].win == focused) {
            client_idx = i;
            break;
        }
    }
    if (client_idx == -1) return;

    int step = 30;
    XWindowAttributes root_attr;
    XGetWindowAttributes(dpy, root, &root_attr);
    int sw = root_attr.width;
    int sh = root_attr.height;

    int master_idx = -1;
    for (int i = 0; i < ws->count; i++) {
        if (!ws->clients[i].floating) {
            master_idx = i;
            break;
        }
    }

    if (master_idx == -1) return;

    if (ws->clients[client_idx].floating) {
        Client *c = &ws->clients[client_idx];
        if (keycode == XKeysymToKeycode(dpy, XK_Left)) c->width -= step;
        if (keycode == XKeysymToKeycode(dpy, XK_Right)) c->width += step;
        if (keycode == XKeysymToKeycode(dpy, XK_Up)) c->height -= step;
        if (keycode == XKeysymToKeycode(dpy, XK_Down)) c->height += step;
        if (c->width < 50) c->width = 50;
        if (c->height < 50) c->height = 50;
        XResizeWindow(dpy, c->win, c->width, c->height);
        return;
    }

    if (client_idx == master_idx) {
        if (!ws->split_vertical) {
            if (keycode == XKeysymToKeycode(dpy, XK_Left)) {
                ws->clients[master_idx].split_ratio -= (float)step / sw;
            } else if (keycode == XKeysymToKeycode(dpy, XK_Right)) {
                ws->clients[master_idx].split_ratio += (float)step / sw;
            }
        } else {
            if (keycode == XKeysymToKeycode(dpy, XK_Up)) {
                ws->clients[master_idx].split_ratio -= (float)step / sh;
            } else if (keycode == XKeysymToKeycode(dpy, XK_Down)) {
                ws->clients[master_idx].split_ratio += (float)step / sh;
            }
        }
    } else {
        if (!ws->split_vertical) {
            if (keycode == XKeysymToKeycode(dpy, XK_Up)) {
                ws->clients[client_idx].secondary_ratio -= (float)step / sh;
            } else if (keycode == XKeysymToKeycode(dpy, XK_Down)) {
                ws->clients[client_idx].secondary_ratio += (float)step / sh;
            }
        } else {
            if (keycode == XKeysymToKeycode(dpy, XK_Left)) {
                ws->clients[client_idx].secondary_ratio -= (float)step / sw;
            } else if (keycode == XKeysymToKeycode(dpy, XK_Right)) {
                ws->clients[client_idx].secondary_ratio += (float)step / sw;
            }
        }
    }

    if (ws->clients[master_idx].split_ratio < 0.1f) ws->clients[master_idx].split_ratio = 0.1f;
    if (ws->clients[master_idx].split_ratio > 0.9f) ws->clients[master_idx].split_ratio = 0.9f;
    if (ws->clients[client_idx].secondary_ratio < 0.1f) ws->clients[client_idx].secondary_ratio = 0.1f;
    if (ws->clients[client_idx].secondary_ratio > 0.9f) ws->clients[client_idx].secondary_ratio = 0.9f;

    tile();
    XSetInputFocus(dpy, focused, RevertToParent, CurrentTime);
    update_borders(focused);
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


static void run_cmd(const char *cmd) {
    if (fork() == 0) {
        setsid();
        execl("/bin/sh", "sh", "-c", cmd, (char *)NULL);
        exit(0);
    }
    while (waitpid(-1, NULL, WNOHANG) > 0);
}


int on_x_error(Display *d, XErrorEvent *e) {
    return 0;
}

int main() {

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
       
    KeyCode term_code     = XKeysymToKeycode(dpy, XK_Return);
    KeyCode fileman_code  = XKeysymToKeycode(dpy, XK_f);
    KeyCode browser_code  = XKeysymToKeycode(dpy, XK_b);
    KeyCode discord_code  = XKeysymToKeycode(dpy, XK_d);
    KeyCode code_code     = XKeysymToKeycode(dpy, XK_c);
    KeyCode audacious_code= XKeysymToKeycode(dpy, XK_m);
    KeyCode vpn_code      = XKeysymToKeycode(dpy, XK_p);
    KeyCode kill_code     = XKeysymToKeycode(dpy, XK_q);
    KeyCode float_code    = XKeysymToKeycode(dpy, XK_z);
    KeyCode swap_code     = XKeysymToKeycode(dpy, XK_e); 
    KeyCode split_code    = XKeysymToKeycode(dpy, XK_s);
    KeyCode stack_toggle  = XKeysymToKeycode(dpy, XK_w);
    KeyCode stack_cycle   = XKeysymToKeycode(dpy, XK_x);
    KeyCode rofi_run_code =  XKeysymToKeycode(dpy, XK_space);
    KeyCode obs_code      = XKeysymToKeycode(dpy, XK_o);
    KeyCode greenclip_code= XKeysymToKeycode(dpy, XK_v);
    KeyCode lock_code     = XKeysymToKeycode(dpy, XK_l);
    KeyCode pavu_code     = XKeysymToKeycode(dpy, XK_a);
    KeyCode escape_code   = XKeysymToKeycode(dpy, XK_Escape);
    KeyCode n_code        = XKeysymToKeycode(dpy, XK_n);
    KeyCode m_code        = XKeysymToKeycode(dpy, XK_m);
    
    KeyCode XF86AudioRaiseVolume_code = XKeysymToKeycode(dpy, XF86XK_AudioRaiseVolume);
    KeyCode XF86AudioLowerVolume_code = XKeysymToKeycode(dpy, XF86XK_AudioLowerVolume);
    KeyCode XF86AudioMute_code        = XKeysymToKeycode(dpy, XF86XK_AudioMute);
    KeyCode XF86AudioMicMute_code     = XKeysymToKeycode(dpy, XF86XK_AudioMicMute);
    KeyCode XF86MonBrightnessUp_code  = XKeysymToKeycode(dpy, XF86XK_MonBrightnessUp);
    KeyCode XF86MonBrightnessDown_code= XKeysymToKeycode(dpy, XF86XK_MonBrightnessDown);
    KeyCode XF86AudioPlay_code        = XKeysymToKeycode(dpy, XF86XK_AudioPlay);
    KeyCode XF86AudioNext_code        = XKeysymToKeycode(dpy, XF86XK_AudioNext);
    KeyCode XF86AudioPrev_code        = XKeysymToKeycode(dpy, XF86XK_AudioPrev);

    KeyCode left_code  = XKeysymToKeycode(dpy, XK_Left);
    KeyCode right_code = XKeysymToKeycode(dpy, XK_Right);
    KeyCode up_code    = XKeysymToKeycode(dpy, XK_Up);
    KeyCode down_code  = XKeysymToKeycode(dpy, XK_Down);

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
          
    for (int i = 0; i < 4; i++) {
        XGrabKey(dpy, term_code, MODKEY | modifiers[i], root, True, GrabModeAsync, GrabModeAsync);
        XGrabKey(dpy, fileman_code, MODKEY | modifiers[i], root, True, GrabModeAsync, GrabModeAsync);
        XGrabKey(dpy, browser_code, MODKEY | modifiers[i], root, True, GrabModeAsync, GrabModeAsync);
        XGrabKey(dpy, discord_code, MODKEY | modifiers[i], root, True, GrabModeAsync, GrabModeAsync);
        XGrabKey(dpy, code_code, MODKEY | modifiers[i], root, True, GrabModeAsync, GrabModeAsync);
        XGrabKey(dpy, audacious_code, MODKEY | modifiers[i], root, True, GrabModeAsync, GrabModeAsync);
        XGrabKey(dpy, vpn_code, MODKEY | modifiers[i], root, True, GrabModeAsync, GrabModeAsync);
        XGrabKey(dpy, kill_code, MODKEY | modifiers[i], root, True, GrabModeAsync, GrabModeAsync);
        XGrabKey(dpy, float_code, MODKEY | modifiers[i], root, True, GrabModeAsync, GrabModeAsync);
        XGrabKey(dpy, swap_code, MODKEY | modifiers[i], root, True, GrabModeAsync, GrabModeAsync);
        XGrabKey(dpy, split_code, MODKEY | modifiers[i], root, True, GrabModeAsync, GrabModeAsync);
        XGrabKey(dpy, stack_toggle, MODKEY | modifiers[i], root, True, GrabModeAsync, GrabModeAsync);
        XGrabKey(dpy, stack_cycle, MODKEY | modifiers[i], root, True, GrabModeAsync, GrabModeAsync);
        XGrabKey(dpy, rofi_run_code, MODKEY | modifiers[i], root, True, GrabModeAsync, GrabModeAsync);
        XGrabKey(dpy, obs_code, MODKEY | modifiers[i], root, True, GrabModeAsync, GrabModeAsync);
        XGrabKey(dpy, greenclip_code, MODKEY | modifiers[i], root, True, GrabModeAsync, GrabModeAsync);
        XGrabKey(dpy, greenclip_code, MODKEY | shift_modifiers[i], root, True, GrabModeAsync, GrabModeAsync);
        XGrabKey(dpy, lock_code, MODKEY | modifiers[i], root, True, GrabModeAsync, GrabModeAsync);
        XGrabKey(dpy, pavu_code, MODKEY | modifiers[i], root, True, GrabModeAsync, GrabModeAsync);
        XGrabKey(dpy, n_code, MODKEY | shift_modifiers[i], root, True, GrabModeAsync, GrabModeAsync);
        XGrabKey(dpy, n_code, MODKEY | modifiers[i], root, True, GrabModeAsync, GrabModeAsync);
        XGrabKey(dpy, m_code, MODKEY | shift_modifiers[i], root, True, GrabModeAsync, GrabModeAsync);
       
       
        XGrabKey(dpy, XKeysymToKeycode(dpy, XK_Escape), MODKEY | modifiers[i], root, True, GrabModeAsync, GrabModeAsync);
        XGrabKey(dpy, XKeysymToKeycode(dpy, XK_s), MODKEY | shift_modifiers[i], root, True, GrabModeAsync, GrabModeAsync);
        XGrabKey(dpy, XKeysymToKeycode(dpy, XK_p), MODKEY | shift_modifiers[i], root, True, GrabModeAsync, GrabModeAsync);
        XGrabKey(dpy, XKeysymToKeycode(dpy, XK_c), MODKEY | shift_modifiers[i], root, True, GrabModeAsync, GrabModeAsync);
        XGrabKey(dpy, XKeysymToKeycode(dpy, XK_u), MODKEY | shift_modifiers[i], root, True, GrabModeAsync, GrabModeAsync);

        XGrabKey(dpy, XF86AudioRaiseVolume_code, 0, root, True, GrabModeAsync, GrabModeAsync);
        XGrabKey(dpy, XF86AudioLowerVolume_code, 0, root, True, GrabModeAsync, GrabModeAsync);
        XGrabKey(dpy, XF86AudioMute_code, 0, root, True, GrabModeAsync, GrabModeAsync);
        XGrabKey(dpy, XF86AudioMicMute_code, 0, root, True, GrabModeAsync, GrabModeAsync);
        XGrabKey(dpy, XF86MonBrightnessUp_code, 0, root, True, GrabModeAsync, GrabModeAsync);
        XGrabKey(dpy, XF86MonBrightnessDown_code, 0, root, True, GrabModeAsync, GrabModeAsync);
        XGrabKey(dpy, XF86AudioPlay_code, 0, root, True, GrabModeAsync, GrabModeAsync);
        XGrabKey(dpy, XF86AudioNext_code, 0, root, True, GrabModeAsync, GrabModeAsync);
        XGrabKey(dpy, XF86AudioPrev_code, 0, root, True, GrabModeAsync, GrabModeAsync);

        XGrabKey(dpy, left_code, MODKEY | modifiers[i], root, True, GrabModeAsync, GrabModeAsync);
        XGrabKey(dpy, right_code, MODKEY | modifiers[i], root, True, GrabModeAsync, GrabModeAsync);
        XGrabKey(dpy, up_code, MODKEY | modifiers[i], root, True, GrabModeAsync, GrabModeAsync);
        XGrabKey(dpy, down_code, MODKEY | modifiers[i], root, True, GrabModeAsync, GrabModeAsync);

        XGrabKey(dpy, float_code, MODKEY | shift_modifiers[i], root, True, GrabModeAsync, GrabModeAsync);
        XGrabKey(dpy, stack_cycle, MODKEY | shift_modifiers[i], root, True, GrabModeAsync, GrabModeAsync);
        
        for (int w = 0; w < NUM_WORKSPACES; w++) {
            XGrabKey(dpy, ws_codes[w], MODKEY | modifiers[i], root, True, GrabModeAsync, GrabModeAsync);
            XGrabKey(dpy, ws_codes[w], MODKEY | shift_modifiers[i], root, True, GrabModeAsync, GrabModeAsync);
        }

        XGrabButton(dpy, Button1, MODKEY | modifiers[i], root, True,
            ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
            GrabModeAsync, GrabModeAsync, None, None);

        XGrabButton(dpy, Button3, MODKEY | modifiers[i], root, True,
            ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
            GrabModeAsync, GrabModeAsync, None, None);
    }

    XSync(dpy, False);
    printf("sehwm running successfully...\n");
  
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
            case ButtonPress: {
                if (ev.xbutton.subwindow != None) {
                    Window w = ev.xbutton.subwindow;
                    Workspace *ws = &workspaces[current_ws];
                    
                    int client_idx = -1;
                    int is_floating = 0;
                    for (int i = 0; i < ws->count; i++) {
                        if (ws->clients[i].win == w) {
                            client_idx = i;
                            is_floating = ws->clients[i].floating;
                            break;
                        }
                    }

                    XWindowAttributes attr;
                    XGetWindowAttributes(dpy, w, &attr);
                    
                    int start_x = ev.xbutton.x_root;
                    int start_y = ev.xbutton.y_root;
                    int start_w = attr.width;
                    int start_h = attr.height;
                    int start_win_x = attr.x;
                    int start_win_y = attr.y;

                    if (ev.xbutton.button == Button1 && is_floating) {
                        XRaiseWindow(dpy, w);
                        XGrabPointer(dpy, root, True,
                            PointerMotionMask | ButtonReleaseMask,
                            GrabModeAsync, GrabModeAsync, None, None, CurrentTime);

                        XEvent event;
                        while (1) {
                            XNextEvent(dpy, &event);
                            if (event.type == MotionNotify) {
                                int dx = event.xmotion.x_root - start_x;
                                int dy = event.xmotion.y_root - start_y;
                                XMoveWindow(dpy, w, start_win_x + dx, start_win_y + dy);
                            } else if (event.type == ButtonRelease) {
                                XUngrabPointer(dpy, CurrentTime);
                                break;
                            }
                        }
                    }
                    else if (ev.xbutton.button == Button3) {
                        if (client_idx != -1) {
                            XRaiseWindow(dpy, w);
                            XGrabPointer(dpy, root, True,
                                PointerMotionMask | ButtonReleaseMask,
                                GrabModeAsync, GrabModeAsync, None, None, CurrentTime);

                            XEvent event;
                            while (1) {
                                XNextEvent(dpy, &event);
                                if (event.type == MotionNotify) {
                                    int dx = event.xmotion.x_root - start_x;
                                    int dy = event.xmotion.y_root - start_y;
                                    int new_w = start_w + dx;
                                    int new_h = start_h + dy;
                                    if (new_w > 50 && new_h > 50) {
                                        XResizeWindow(dpy, w, new_w, new_h);
                                        if (is_floating) {
                                            ws->clients[client_idx].width = new_w;
                                            ws->clients[client_idx].height = new_h;
                                        }
                                    }
                                } else if (event.type == ButtonRelease) {
                                    XUngrabPointer(dpy, CurrentTime);
                                    break;
                                }
                            }
                        }
                    }
                }
                break;
            }
            case KeyPress: {
                if (ev.xkey.keycode == term_code) {
                    run_cmd("alacritty");
                } else if (ev.xkey.keycode == fileman_code) {
                    run_cmd("thunar");
                } else if (ev.xkey.keycode == browser_code) {
                    run_cmd("helium-browser");
                } else if (ev.xkey.keycode == discord_code) {
                    run_cmd("discord");
                } else if (ev.xkey.keycode == pavu_code) {
                    run_cmd("pavucontrol");
                } else if (ev.xkey.keycode == code_code && !(ev.xkey.state & ShiftMask)) {
                    run_cmd("code");
                } else if (ev.xkey.keycode == audacious_code && !(ev.xkey.state & ShiftMask)) {
                    run_cmd("audacious");
                } else if (ev.xkey.keycode == vpn_code && !(ev.xkey.state & ShiftMask)) {
                    run_cmd("protonvpn-app");
                } else if (ev.xkey.keycode == obs_code && !(ev.xkey.state & ShiftMask)) {
                    run_cmd("obs");
                } else if (ev.xkey.keycode == greenclip_code && !(ev.xkey.state & ShiftMask)) {
                    run_cmd("rofi -modi \"clipboard:greenclip print\" -show clipboard");
                } else if (ev.xkey.keycode == greenclip_code && (ev.xkey.state & ShiftMask)) {
                    run_cmd("greenclip clear");
                } else if (ev.xkey.keycode == lock_code) {
                    run_cmd("loginctl lock-session");
                } else if (ev.xkey.keycode == rofi_run_code && !(ev.xkey.state & ShiftMask)) {
                    run_cmd("rofi -show drun");
                } else if (ev.xkey.keycode == XKeysymToKeycode(dpy, XK_s) && (ev.xkey.state & ShiftMask)) {
                    run_cmd("maim -s | xclip -selection clipboard -t image/png");
                } else if (ev.xkey.keycode == XKeysymToKeycode(dpy, XK_p) && (ev.xkey.state & ShiftMask)) {
                    run_cmd("sh -c '$HOME/.config/scripts/power_profile.sh'");
                } else if (ev.xkey.keycode == XKeysymToKeycode(dpy, XK_Escape)) {
                    run_cmd("sh -c '$HOME/.config/scripts/rofi-powermenu.sh'");                
                } else if (ev.xkey.keycode == XKeysymToKeycode(dpy, XK_u) && (ev.xkey.state & ShiftMask)) {
                    run_cmd("alacritty -e sh -c '$HOME/.config/scripts/system_update.sh; echo \"sehwm\"; read'");  
                } else if (ev.xkey.keycode == XKeysymToKeycode(dpy, XK_c) && (ev.xkey.state & ShiftMask)) {
                    run_cmd("alacritty -e sh -c '$HOME/.config/scripts/system_clean.sh; echo \"sehwm\"; read'");
                } else if (ev.xkey.keycode == n_code && (ev.xkey.state & ShiftMask)) {
                    run_cmd("alacritty -e nmtui");
                } else if (ev.xkey.keycode == m_code && (ev.xkey.state & ShiftMask)) {
                    run_cmd("sh -c '$HOME/.config/scripts/sysmenu.sh'");
                } else if (ev.xkey.keycode == XF86AudioRaiseVolume_code) {
                    run_cmd("pactl set-sink-volume @DEFAULT_SINK@ +5%");
                } else if (ev.xkey.keycode == XF86AudioLowerVolume_code) {
                    run_cmd("pactl set-sink-volume @DEFAULT_SINK@ -5%");
                } else if (ev.xkey.keycode == XF86AudioMute_code) {
                    run_cmd("pactl set-sink-mute @DEFAULT_SINK@ toggle");
                } else if (ev.xkey.keycode == XF86AudioMicMute_code) {
                    run_cmd("pactl set-source-mute @DEFAULT_SOURCE@ toggle");
                } else if (ev.xkey.keycode == XF86MonBrightnessUp_code) {
                    run_cmd("brightnessctl set +5%");
                } else if (ev.xkey.keycode == XF86MonBrightnessDown_code) {
                    run_cmd("brightnessctl set 5%-");
                } else if (ev.xkey.keycode == XF86AudioPlay_code) {
                    run_cmd("playerctl play-pause");
                } else if (ev.xkey.keycode == XF86AudioNext_code) {
                    run_cmd("playerctl next");
                } else if (ev.xkey.keycode == XF86AudioPrev_code) {
                    run_cmd("playerctl previous");
                } else if (ev.xkey.keycode == float_code) {
                    if (ev.xkey.state & ShiftMask) {
                        toggle_float_all();
                    } else {
                        toggle_float_single();
                    }
                } else if (ev.xkey.keycode == swap_code) {
                    swap_windows();
                } else if (ev.xkey.keycode == split_code) {
                    Workspace *ws = &workspaces[current_ws];
                    if (current_ws != SCRATCHPAD_WS && !ws->stacked_mode) {
                        ws->split_vertical = !ws->split_vertical;
                        tile();
                    }
                } else if (ev.xkey.keycode == stack_toggle) {
                    toggle_stacked();
                } else if (ev.xkey.keycode == stack_cycle) {
                    if (ev.xkey.state & ShiftMask) {
                        cycle_stack(-1);
                    } else {
                        cycle_stack(1);
                    }
                } else if (ev.xkey.keycode == left_code || ev.xkey.keycode == right_code || 
                           ev.xkey.keycode == up_code || ev.xkey.keycode == down_code) {
                    int dir = (ev.xkey.keycode == left_code || ev.xkey.keycode == up_code) ? 0 : 1;
                    if (ev.xkey.state & ShiftMask) {
                        swap_windows_direction(dir);
                    } else {
                        resize_window_direction(ev.xkey.keycode);
                    }
                } else if (ev.xkey.keycode == kill_code) {
                    Window focused;
                    int revert;
                    XGetInputFocus(dpy, &focused, &revert);
                    if (focused != root && focused != None) {
                        Atom wm_protocols = XInternAtom(dpy, "WM_PROTOCOLS", False);
                        Atom wm_delete_window = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
                        Atom *protocols = NULL;
                        int n_protocols = 0;
                        int sent = 0;

                        if (XGetWMProtocols(dpy, focused, &protocols, &n_protocols)) {
                            for (int i = 0; i < n_protocols; i++) {
                                if (protocols[i] == wm_delete_window) {
                                    XEvent ke_real;
                                    ke_real.type = ClientMessage;
                                    ke_real.xclient.window = focused;
                                    ke_real.xclient.message_type = wm_protocols;
                                    ke_real.xclient.format = 32;
                                    ke_real.xclient.data.l[0] = wm_delete_window;
                                    ke_real.xclient.data.l[1] = CurrentTime;
                                    XSendEvent(dpy, focused, False, NoEventMask, &ke_real);
                                    sent = 1;
                                    break;
                                }
                            }
                            if (protocols) XFree(protocols);
                        }
                        if (!sent) {
                            XKillClient(dpy, focused);
                        }
                    }
                } else {
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
