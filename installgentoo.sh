#!/bin/bash

# Exit immediately if a command exits with a non-zero status
set -e

# Get the directory where this script is located
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

echo "Starting minimal automated Gentoo installation..."

# 1. Create standard home directories
echo "Creating user directories..."
mkdir -p "$HOME/Documents" "$HOME/Music" "$HOME/Downloads" "$HOME/Pictures" "$HOME/Videos" "$HOME/.config"

# 2. Update local Portage packages list and dependencies
echo "Updating Portage world requirements..."

# Inject lightweight PolicyKit, Clipmenu, Dunst, and Thunar capabilities into your system
# Run this via sudo inside the script to ensure your environment accepts the packages
sudo emerge --ask=n --noreplace \
    x11-base/xorg-server x11-apps/xinit x11-apps/xrandr x11-libs/libXinerama x11-libs/libXft \
    media-fonts/liberation-fonts media-fonts/symbols-nerd-font x11-misc/rofi media-gfx/feh \
    xfce-base/thunar xfce-extra/tumblr sys-fs/udisks x11-themes/adwaita-icon-theme \
    media-gfx/imv media-video/mpv media-sound/pavucontrol x11-misc/dunst \
    x11-misc/clipmenu x11-misc/xsel x11-misc/xclip sys-auth/lxqt-policykit \
    media-sound/playerctl sys-power/brightnessctl media-sound/cava sys-process/btop \
    app-arch/zip app-arch/unzip app-editors/micro app-editors/nano \
    x11-misc/slstatus media-gfx/maim x11-misc/picom

# 3. Setup Helium Browser (Safe pre-compiled AppImage deployment)
echo "Setting up Helium Browser..."
mkdir -p "$HOME/.local/bin"
if [ ! -f "$HOME/.local/bin/helium" ]; then
    wget https://github.com -O "$HOME/.local/bin/helium"
    chmod +x "$HOME/.local/bin/helium"
fi

# 4. Copy configuration files (.config directory)
echo "Copying config files to $HOME/.config/..."
if [ -d "$SCRIPT_DIR/config" ]; then
    cp -r "$SCRIPT_DIR/config/"* "$HOME/.config/"
else
    echo "Warning: No config folder found in repository!"
fi

# 5. Copy DWM and ST to $HOME, compile them, and fix ownership permissions
echo "Copying and compiling DWM & ST in $HOME..."

if [ -d "$SCRIPT_DIR/dwm" ]; then
    rm -rf "$HOME/dwm"
    cp -r "$SCRIPT_DIR/dwm" "$HOME/"
    cd "$HOME/dwm"
    sudo make clean install
    sudo chown -R "$USER:$USER" "$HOME/dwm"
else
    echo "Error: dwm directory not found in repository!"
fi

cd "$SCRIPT_DIR"

if [ -d "$SCRIPT_DIR/st" ]; then
    rm -rf "$HOME/st"
    cp -r "$SCRIPT_DIR/st" "$HOME/"
    cd "$HOME/st"
    sudo make clean install
    sudo chown -R "$USER:$USER" "$HOME/st"
else
    echo "Error: st directory not found in repository!"
fi

cd "$SCRIPT_DIR"

# 6. Setup DWM startup script (~/.xinitrc) for use with startx
echo "Setting up X11 startup script (~/.xinitrc)..."
cat << 'EOF' > "$HOME/.xinitrc"
#!/bin/sh

# 1. Establish secure local D-Bus session communication
if [ -z "$DBUS_SESSION_BUS_ADDRESS" ]; then
    eval $(dbus-launch --sh-syntax --exit-with-session)
fi

# 2. Run independent visual background apps
feh --bg-scale "$HOME/Pictures/main.png" &
picom &

# 3. Launch background service daemons 
thunar --daemon &
dunst &
clipmenud &
slstatus &

# 4. Launch structural Polkit permission agent securely
/usr/libexec/lxqt-policykit-agent &

# 5. Start the core window manager through systemd session tracking
exec dbus-run-session dwm
EOF

chmod +x "$HOME/.xinitrc"

# 7. Copy wallpaper to Pictures directory
if [ -f "$SCRIPT_DIR/main.png" ]; then
    echo "Copying wallpaper to $HOME/Pictures/main.png..."
    cp "$SCRIPT_DIR/main.png" "$HOME/Pictures/main.png"
fi

# 8. Make custom scripts executable
if [ -d "$HOME/.config/scripts" ]; then
    chmod +x "$HOME/.config/scripts/"*
fi

# 9. Dynamically fix home paths in configs for current user
echo "Fixing home directory paths for $USER..."
find "$HOME/.config" -type f -exec sed -i "s|/home/[^/]*|$HOME|g" {} + 2>/dev/null || true

echo "Installation Complete! Type 'startx' to enter your clean DWM environment."
