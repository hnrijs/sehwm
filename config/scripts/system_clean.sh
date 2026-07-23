#!/bin/bash

echo "Starting System Cleanup"

# Clean pacman cache keeping only the latest versions
echo "1. Cleaning Pacman cache..."
sudo paccache -r

# Remove orphaned packages (unused dependencies)
if [ -n "$(pacman -Qtdq)" ]; then
    echo "2. Removing orphaned packages..."
    sudo pacman -Rns $(pacman -Qtdq) --noconfirm
else
    echo "2. No orphaned packages found."
fi

# Clean yay/AUR cache
echo "3. Cleaning AUR cache..."
yay -Scc --noconfirm

# Clean user thumbnail cache
echo "4. Cleaning thumbnail cache..."
rm -rf ~/.cache/thumbnails/*

echo "Cleanup Complete!"
read -p "Press [Enter] to close..."
