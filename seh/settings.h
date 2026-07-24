#ifndef SETTINGS_H
#define SETTINGS_H
#include <X11/XF86keysym.h>
#define BORDER_WIDTH 2         
#define COLOR_FOCUSED 0xFFFFFF   
#define COLOR_UNFOCUSED 0x000000 
#define GAP_INNER 5              
#define GAP_TOP 40               
#define GAP_SIDE 5               
#define NUM_WORKSPACES 10        
#define SCRATCHPAD_WS 9          
#define MODKEY Mod4Mask          

typedef enum {
    CMD_SPAWN,
    FUNC_KILL,
    FUNC_FLOAT_SINGLE,
    FUNC_FLOAT_ALL,
    FUNC_SWAP,
    FUNC_SPLIT,
    FUNC_STACK_TOGGLE,
    FUNC_STACK_CYCLE,
    FUNC_DIR_SWAP,
    FUNC_DIR_RESIZE
} BindType;

typedef struct {
    unsigned int mod;
    KeySym keysym;
    BindType type;
    const char *arg;
    int int_arg;
} Key;


static void spawn_cmd(const char *arg) { run_cmd(arg); }
static const Key keys[] = {

    { MODKEY,            XK_Return,      CMD_SPAWN,         "alacritty", 0 },
    { MODKEY,            XK_f,           CMD_SPAWN,         "thunar", 0 },
    { MODKEY,            XK_b,           CMD_SPAWN,         "helium-browser", 0 },
    { MODKEY,            XK_d,           CMD_SPAWN,         "discord", 0 },
    { MODKEY,            XK_c,           CMD_SPAWN,         "code", 0 },
    { MODKEY,            XK_m,           CMD_SPAWN,         "audacious", 0 },
    { MODKEY,            XK_p,           CMD_SPAWN,         "protonvpn-app", 0 },
    { MODKEY,            XK_o,           CMD_SPAWN,         "obs", 0 },
    { MODKEY,            XK_space,       CMD_SPAWN,         "rofi -show drun", 0 },
    { MODKEY,            XK_v,           CMD_SPAWN,         "rofi -modi \"clipboard:greenclip print\" -show clipboard", 0 },
    { MODKEY|ShiftMask,  XK_v,           CMD_SPAWN,         "greenclip clear", 0 },
    { MODKEY,            XK_l,           CMD_SPAWN,         "loginctl lock-session", 0 },
    { MODKEY,            XK_a,           CMD_SPAWN,         "pavucontrol", 0 },
    { MODKEY|ShiftMask,  XK_s,           CMD_SPAWN,         "maim -s | xclip -selection clipboard -t image/png", 0 },
    { MODKEY|ShiftMask,  XK_p,           CMD_SPAWN,         "sh -c '$HOME/.config/scripts/power_profile.sh'", 0 },
    { MODKEY,            XK_Escape,      CMD_SPAWN,         "sh -c '$HOME/.config/scripts/rofi-powermenu.sh'", 0 },
    { MODKEY|ShiftMask,  XK_u,           CMD_SPAWN,         "alacritty -e sh -c '$HOME/.config/scripts/system_update.sh; echo \"sehwm\"; read'", 0 },
    { MODKEY|ShiftMask,  XK_c,           CMD_SPAWN,         "alacritty -e sh -c '$HOME/.config/scripts/system_clean.sh; echo \"sehwm\"; read'", 0 },
    { MODKEY|ShiftMask,  XK_n,           CMD_SPAWN,         "alacritty -e nmtui", 0 },
    { MODKEY|ShiftMask,  XK_m,           CMD_SPAWN,         "sh -c '$HOME/.config/scripts/sysmenu.sh'", 0 },

    { MODKEY,            XK_q,           FUNC_KILL,         NULL, 0 },
    { MODKEY,            XK_z,           FUNC_FLOAT_SINGLE, NULL, 0 },
    { MODKEY|ShiftMask,  XK_z,           FUNC_FLOAT_ALL,    NULL, 0 },
    { MODKEY,            XK_e,           FUNC_SWAP,         NULL, 0 },
    { MODKEY,            XK_s,           FUNC_SPLIT,        NULL, 0 },
    { MODKEY,            XK_w,           FUNC_STACK_TOGGLE, NULL, 0 },
    { MODKEY,            XK_x,           FUNC_STACK_CYCLE,  NULL, 1 },    
    { MODKEY|ShiftMask,  XK_x,           FUNC_STACK_CYCLE,  NULL, -1 },   

    { MODKEY,            XK_Left,        FUNC_DIR_RESIZE,   NULL, -1 }, 
    { MODKEY,            XK_Right,       FUNC_DIR_RESIZE,   NULL, 1 },  
    { MODKEY,            XK_Up,          FUNC_DIR_RESIZE,   NULL, -1 }, 
    { MODKEY,            XK_Down,        FUNC_DIR_RESIZE,   NULL, 1 },  
    
    { 0, XF86XK_AudioRaiseVolume,    CMD_SPAWN, "pactl set-sink-volume @DEFAULT_SINK@ +5%", 0 },
    { 0, XF86XK_AudioLowerVolume,    CMD_SPAWN, "pactl set-sink-volume @DEFAULT_SINK@ -5%", 0 },
    { 0, XF86XK_AudioMute,           CMD_SPAWN, "pactl set-sink-mute @DEFAULT_SINK@ toggle", 0 },
    { 0, XF86XK_AudioMicMute,        CMD_SPAWN, "pactl set-source-mute @DEFAULT_SOURCE@ toggle", 0 },
    { 0, XF86XK_MonBrightnessUp,     CMD_SPAWN, "brightnessctl set +5%", 0 },
    { 0, XF86XK_MonBrightnessDown,   CMD_SPAWN, "brightnessctl set 5%-", 0 },
    { 0, XF86XK_AudioPlay,           CMD_SPAWN, "playerctl play-pause", 0 },
    { 0, XF86XK_AudioNext,           CMD_SPAWN, "playerctl next", 0 },
    { 0, XF86XK_AudioPrev,           CMD_SPAWN, "playerctl previous", 0 },
};

#endif
