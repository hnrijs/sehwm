#!/bin/bash

# Define the main options list for the Rofi system menu with all features included
options="Reload SEHWM\nConfigure SEHWM\nConfigure Network\nConfigure Bluetooth\nConfigure Monitors\nConfigure Keyboard\nSystem Clean\nSystem Update"

# Launch main rofi menu and capture choice
chosen="$(echo -e "$options" | rofi -dmenu -p "System Menu")"

# Handle the user's selection using a case statement
case "$chosen" in
    "Reload SEHWM")
        pkill -HUP sehwm || echo "SEHWM is not running"
        ;;
    "Configure SEHWM")
        alacritty -e nano "$HOME/seh/config.c"
        ;;
    "Configure Network")
        # Sub-menu for network actions ordered as requested: Open, Enable, Disable, Remove
        net_options="Open NMTUI\nEnable Network\nDisable Network\nRemove Network"
        net_chosen="$(echo -e "$net_options" | rofi -dmenu -p "Network Menu")"
        
        case "$net_chosen" in
            "Open NMTUI")
                alacritty -e nmtui
                ;;
            "Enable Network")
                sudo systemctl enable --now NetworkManager
                ;;
            "Disable Network")
                sudo systemctl disable --now NetworkManager
                ;;
            "Remove Network")
                sudo systemctl disable --now NetworkManager &>/dev/null
                sudo pacman -Rdd --noconfirm networkmanager &>/dev/null
                ;;
        esac
        ;;
    "Configure Bluetooth")
        # Sub-menu for Bluetooth actions ordered as requested: Open, Enable, Disable, Remove
        bt_options="Open Bluetui\nEnable Bluetooth\nDisable Bluetooth\nRemove Bluetooth"
        bt_chosen="$(echo -e "$bt_options" | rofi -dmenu -p "Bluetooth Menu")"
        
        case "$bt_chosen" in
            "Open Bluetui")
                if ! pacman -Q bluez bluez-utils bluetui &>/dev/null; then
                    alacritty -e sudo pacman -S --noconfirm bluez bluez-utils bluetui
                fi
                if ! systemctl is-active --quiet bluetooth; then
                    sudo systemctl enable --now bluetooth
                fi
                alacritty -e bluetui
                ;;
            "Enable Bluetooth")
                if ! pacman -Q bluez bluez-utils bluetui &>/dev/null; then
                    alacritty -e sudo pacman -S --noconfirm bluez bluez-utils bluetui
                fi
                sudo systemctl enable --now bluetooth
                ;;
            "Disable Bluetooth")
                sudo systemctl disable --now bluetooth
                ;;
            "Remove Bluetooth")
                sudo systemctl disable --now bluetooth &>/dev/null
                sudo pacman -Rdd --noconfirm bluez bluez-utils bluetui &>/dev/null
                ;;
        esac
        ;;
    "Configure Monitors")
        mon_options="Open ARandR (GUI)\nList Monitors (CLI)"
        mon_chosen="$(echo -e "$mon_options" | rofi -dmenu -p "Monitor Menu")"
        
        case "$mon_chosen" in
            "Open ARandR (GUI)")
                if ! pacman -Q arandr &>/dev/null; then
                    alacritty -e sudo pacman -S --noconfirm arandr
                fi
                arandr &
                ;;
            "List Monitors (CLI)")
                alacritty -e sh -c "xrandr || wlr-randr; echo ''; echo 'Press enter to close'; read"
                ;;
        esac
        ;;
    "Configure Keyboard")
        kbd_options="Set US Layout\nSet LV Layout\nCustom Localectl Status"
        kbd_chosen="$(echo -e "$kbd_options" | rofi -dmenu -p "Keyboard Menu")"
        
        case "$kbd_chosen" in
            "Set US Layout")
                localectl set-x11-keymap us
                ;;
            "Set LV Layout")
                localectl set-x11-keymap lv
                ;;
            "Custom Localectl Status")
                alacritty -e sh -c "localectl status; echo ''; echo 'Press enter to close'; read"
                ;;
        esac
        ;;
    "System Clean")
        alacritty -e sh -c "$HOME/.config/scripts/system_clean.sh"
        ;;
    "System Update")
        alacritty -e sh -c "$HOME/.config/scripts/system_update.sh"
        ;;
esac
