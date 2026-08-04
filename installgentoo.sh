#!/bin/bash

# Exit immediately if a command exits with a non-zero status
set -e

# Get the directory where this script is located
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

echo "Starting automated Gentoo deployment layout..."

# 0. Ensure git is installed first
if ! command -v git &> /dev/null; then
    echo "Installing git..."
    sudo emerge --ask=n dev-vcs/git
fi

# 1. Create standard home directories
echo "Creating user directories..."
mkdir -p "$HOME/Documents" "$HOME/Music" "$HOME/Downloads" "$HOME/Pictures" "$HOME/Videos" "$HOME/.config"

# 2. Update system and install official Gentoo binary packages
echo "Installing official Gentoo binaries..."
sudo emerge --ask=n \
    sys-devel/gcc x11-base/xorg-server x11-apps/xinit x11-libs/libX11 \
    x11-libs/libXft x11-libs/libXinerama x11-libs/libXrandr media-libs/fontconfig \
    media-libs/freetype media-gfx/feh x11-misc/rofi media-gfx/imv media-sound/cava \
    sys-process/btop media-sound/playerctl gui-apps/alacritty app-arch/zip \
    app-arch/unzip gnome-base/polkit-gnome x11-misc/xclip x11-misc/maim \
    media-fonts/jetbrainsmono-nerdfont media-fonts/noto-emoji x11-libs/gtk+ \
    app-misc/fastfetch media-video/mpv sys-power/brightnessctl x11-misc/xsettingsd \
    app-editors/micro app-editors/nano x11-apps/xrandr x11-misc/polybar \
    x11-misc/dunst x11-apps/xinput x11-misc/xsecurelock net-misc/curl \
    app-misc/jq x11-misc/xdg-utils app-text/tesseract media-gfx/imagemagick \
    x11-libs/libnotify app-editors/vim x11-misc/xsel x11-misc/xdotool \
    x11-misc/picom media-fonts/dejavu media-fonts/font-awesome media-fonts/noto \
    gnome-base/gvfs sys-fs/udisks x11-misc/clipmenu sys-apps/dbus x11-misc/lightdm \
    x11-misc/lightdm-gtk-greeter x11-themes/materia-theme

# 3 & 4. Install Firefox via Binary distribution matching custom build configurations
if ! command -v firefox-bin &> /dev/null; then
    echo "Installing Firefox binary distribution package..."
    sudo emerge --ask=n www-client/firefox-bin
fi

# 5. Copy configuration files (.config directory)
echo "Copying config files to $HOME/.config/..."
if [ -d "$SCRIPT_DIR/config" ]; then
    cp -r "$SCRIPT_DIR/config/"* "$HOME/.config/"
else
    echo "Warning: No config folder found in repository!"
fi

# 6. Copy 'seh' folder to $HOME, compile config.c into sehwm, and fix permissions
echo "Copying and compiling window manager from 'seh' folder..."
if [ -d "$SCRIPT_DIR/seh" ]; then
    rm -rf "$HOME/seh"
    cp -r "$SCRIPT_DIR/seh" "$HOME/seh"
    
    cd "$HOME/seh"
    gcc config.c -o sehwm -lX11 -lXinerama -lXrandr -lXft -lfontconfig -I/usr/include/freetype2
    sudo chown -R "$USER:$USER" "$HOME/seh"
else
    echo "Error: seh directory not found in repository!"
fi

cd "$SCRIPT_DIR"

# 7. Disable mouse acceleration globally
echo "Disabling mouse acceleration globally..."
sudo mkdir -p /etc/X11/xorg.conf.d
cat << 'EOF' | sudo tee /etc/X11/xorg.conf.d/50-mouse-acceleration.conf > /dev/null
Section "InputClass"
    Identifier "My Mouse"
    MatchIsPointer "yes"
    Option "AccelProfile" "flat"
    Option "AccelSpeed" "0"
EndSection
EOF

# 8. Setup .xprofile for LightDM
echo "Setting up X11 startup script (.xprofile)..."
cat << 'EOF' > "$HOME/.xprofile"
#!/bin/bash
if [ -f "$HOME/.Xresources" ]; then
    xrdb -merge "$HOME/.Xresources"
fi
export XCURSOR_SIZE=24
export XCURSOR_THEME="Adwaita"
export CM_LAUNCHER=rofi
export CM_SELECTIONS="clipboard"
feh --bg-scale "$HOME/Pictures/main.png" &
$HOME/.config/scripts/polybar.sh &
$HOME/.config/scripts/screen.sh &
clipmenud &
dunst &
picom &
/usr/libexec/polkit-gnome-authentication-agent-1 &
EOF
chmod +x "$HOME/.xprofile"

# 9. Create xsessions entry for LightDM
echo "Creating SEHWM desktop session for LightDM..."
sudo mkdir -p /usr/share/xsessions
cat << EOF | sudo tee /usr/share/xsessions/sehwm.desktop > /dev/null
[Desktop Entry]
Name=sehwm
Comment=SEH Window Manager
Exec=$HOME/seh/sehwm
Type=Application
X-LightDM-DesktopName=sehwm
DesktopNames=sehwm
EOF

# 10. Copy wallpaper to Pictures directory
if [ -f "$SCRIPT_DIR/main.png" ]; then
    echo "Copying wallpaper to $HOME/Pictures/main.png..."
    cp "$SCRIPT_DIR/main.png" "$HOME/Pictures/main.png"
fi

# 11. Make custom scripts executable
if [ -d "$HOME/.config/scripts" ]; then
    chmod +x "$HOME/.config/scripts/"*
fi

# 12. Dynamically fix home paths in configs
echo "Fixing home paths in configurations for $USER..."
find "$HOME/.config" -type f -exec sed -i "s|/home/[^/]*|$HOME|g" {} + 2>/dev/null || true

# 13. Add sehwm-update alias to .bashrc
echo "Adding sehwm-update alias..."
cat << 'EOF' >> "$HOME/.bashrc"
alias usehwm='cd "$HOME/seh" && gcc config.c -o sehwm -lX11 -lXinerama -lXrandr -lXft -lfontconfig -I/usr/include/freetype2'
EOF

# 14. Enable system services via systemctl (Systemd)
echo "Enabling core daemons..."
sudo systemctl enable dbus.service
sudo systemctl enable lightdm.service

echo "Installation complete! Rebooting system in 5 seconds..."
sleep 5
sudo reboot
