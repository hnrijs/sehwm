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
    xclip maim ttf-jetbrains-mono-nerd noto-fonts-emoji \
    gtk3 fastfetch pavucontrol nwg-look mpv brightnessctl xsettingsd micro \
    networkmanager network-manager-applet lightdm lightdm-gtk-greeter nano

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

# 4. Install AUR packages (including slstatus)
echo "Installing AUR packages..."
yay -S --noconfirm helium-browser-bin rofi-greenclip slstatus

# 5. Copy configuration files (.config directory)
echo "Copying config files to $HOME/.config/..."
if [ -d "$SCRIPT_DIR/config" ]; then
    cp -r "$SCRIPT_DIR/config/"* "$HOME/.config/"
else
    echo "Warning: No config folder found in repository!"
fi

# 6. Copy SEHWM to $HOME, compile it, and fix ownership permissions
echo "Copying and compiling SEHWM in $HOME..."

if [ -d "$SCRIPT_DIR/sehwm" ]; then
    cp -r "$SCRIPT_DIR/sehwm" "$HOME/"
    cd "$HOME/sehwm"
    
    gcc sehwm.c -o sehwm -lX11
    sudo chown -R "$USER:$USER" "$HOME/sehwm"
else
    echo "Error: sehwm directory not found in repo!"
fi

cd "$SCRIPT_DIR"

# 7. Setup SEHWM startup files (.xinitrc and .xsession) with slstatus
echo "Setting up X11 startup scripts..."
cat << 'EOF' > "$HOME/.xinitrc"
#!/bin/bash

# Autostart background processes & wallpaper
feh --bg-scale "$HOME/Pictures/main.png" &
dex --autostart --environment sehwm &
nm-applet &
/usr/lib/polkit-gnome/polkit-gnome-authentication-agent-1 &

# Start slstatus for date and time in the status bar
slstatus &

# Launch SEHWM
exec "$HOME/sehwm/sehwm"
EOF

cp "$HOME/.xinitrc" "$HOME/.xsession"
chmod +x "$HOME/.xinitrc" "$HOME/.xsession"

# Setup .xprofile
cat << 'EOF' > "$HOME/.xprofile"
#!/bin/bash
feh --bg-scale "$HOME/Pictures/main.png" &
nm-applet &
/usr/lib/polkit-gnome/polkit-gnome-authentication-agent-1 &
slstatus &
EOF
chmod +x "$HOME/.xprofile"

# 8. Create xsessions entry for LightDM
echo "Creating SEHWM desktop session for LightDM..."
sudo mkdir -p /usr/share/xsessions
cat << 'EOF' | sudo tee /usr/share/xsessions/sehwm.desktop > /dev/null
[Desktop Entry]
Name=sehwm
Comment=Custom C X11 Window Manager
Exec=/home/$USER/sehwm/sehwm
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
echo "Fixing home directory paths for $USER..."
find "$HOME/.config" -type f -exec sed -i "s|/home/[^/]*|$HOME|g" {} + 2>/dev/null || true

# 12. Add sehwm-update alias to .bashrc
echo "Adding sehwm-update alias..."
cat << 'EOF' >> "$HOME/.bashrc"
# Custom alias to quickly re-compile sehwm
alias sehwm-update='cd "$HOME/sehwm" && gcc sehwm.c -o sehwm -lX11 && echo "SEHWM successfully updated!"'
EOF

# 13. Enable system and user services
echo "Enabling services..."
systemctl --user enable --now greenclip.service || true
sudo systemctl enable --now NetworkManager
sudo systemctl enable lightdm

echo "Installation Complete! Rebooting..."
sleep 5
sudo reboot
