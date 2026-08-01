#!/bin/bash

# Exit immediately if a command exits with a non-zero status
set -e

# Get the directory where this script is located
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

echo "Starting minimal automated installation..."

# 0. Ensure git is installed first
if ! command -v git &> /dev/null; then
    echo "Installing git..."
    sudo pacman -Sy --noconfirm git
fi

# 1. Create standard home directories
echo "Creating user directories..."
mkdir -p "$HOME/Documents" "$HOME/Music" "$HOME/Downloads" "$HOME/Pictures" "$HOME/Videos" "$HOME/.config"

# 2. Update system and install official pacman packages
echo "Installing official pacman packages..."
sudo pacman -S --needed --noconfirm \
    base-devel wget xorg-server xorg-xinit libx11 libxft libxinerama \
    feh thunar rofi imv cava btop playerctl alacritty zip unzip polkit-gnome \
    xclip maim ttf-jetbrains-mono-nerd noto-fonts-emoji  ttf-nerd-fonts-symbols \
    gtk3 fastfetch pavucontrol nwg-look mpv brightnessctl xsettingsd micro nano  \
    xorg-xrandr power-profiles-daemon python-gobject arandr polybar \
    lightdm lightdm-gtk-greeter materia-theme dunst xorg-xinput xsecurelock \
    curl jq xdg-utils tesseract tesseract-data-eng imagemagick libnotify vim \
    clipmenu xsel xdotool

# 3. Check and install yay AUR helper
if ! command -v yay &> /dev/null; then
    echo "Installing yay AUR helper..."
    mkdir -p /tmp/yay-build
    git clone https://aur.archlinux.org/yay.git /tmp/yay-build/yay
    cd /tmp/yay-build/yay
    makepkg -si --noconfirm
    cd -
    rm -rf /tmp/yay-build
fi

# 4. Install AUR packages
echo "Installing AUR packages..."
yay -S --noconfirm helium-browser-bin

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
    
    # Enter directory, compile config.c into sehwm, and set permissions
    cd "$HOME/seh"
    gcc config.c -o sehwm -lX11 -lXinerama -lXrandr -lXft -lfontconfig -I/usr/include/freetype2
    sudo chown -R "$USER:$USER" "$HOME/seh"
else
    echo "Error: seh directory not found in repository!"
fi

cd "$SCRIPT_DIR"

# 7. Setup SEHWM startup files (.xinitrc and .xsession)
echo "Setting up X11 startup scripts..."
cat << 'EOF' > "$HOME/.xinitrc"
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
xinput --set-prop $(xinput list | grep -i "mouse" | head -n 1 | grep -o 'id=[0-9]*' | cut -d= -f2) "libinput Accel Profile Enabled" 0, 1, 0 &
/usr/lib/polkit-gnome/polkit-gnome-authentication-agent-1 &

# Launch SEHWM
exec "$HOME/seh/sehwm"
EOF

cp "$HOME/.xinitrc" "$HOME/.xsession"
chmod +x "$HOME/.xinitrc" "$HOME/.xsession"

# Setup .xprofile for LightDM
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
xinput --set-prop $(xinput list | grep -i "mouse" | head -n 1 | grep -o 'id=[0-9]*' | cut -d= -f2) "libinput Accel Profile Enabled" 0, 1, 0 &
/usr/lib/polkit-gnome/polkit-gnome-authentication-agent-1 &
EOF
chmod +x "$HOME/.xprofile"

# 8. Create xsessions entry for LightDM (using EOF without quotes so $HOME expands to your actual path)
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

# 9. Copy wallpaper to Pictures directory
if [ -f "$SCRIPT_DIR/main.png" ]; then
    echo "Copying wallpaper to $HOME/Pictures/main.png..."
    cp "$SCRIPT_DIR/main.png" "$HOME/Pictures/main.png"
fi

# 10. Make custom scripts executable
if [ -d "$HOME/.config/scripts" ]; then
    chmod +x "$HOME/.config/scripts/"*
fi

# 11. Dynamically fix home paths in configs for current user
echo "Fixing home paths in configurations for $USER..."
find "$HOME/.config" -type f -exec sed -i "s|/home/[^/]*|$HOME|g" {} + 2>/dev/null || true

# 12. Add sehwm-update alias to .bashrc
echo "Adding sehwm-update alias..."
cat << 'EOF' >> "$HOME/.bashrc"
alias usehwm='cd "$HOME/seh" && gcc config.c -o sehwm -lX11 -lXinerama -lXrandr -lXft -lfontconfig -I/usr/include/freetype2'
EOF

# 13. Enable system and user services
echo "Enabling services..."
sudo systemctl enable --now power-profiles-daemon
sudo systemctl enable lightdm

echo "Installation complete! Rebooting system in 5 seconds..."
sleep 5
sudo reboot
