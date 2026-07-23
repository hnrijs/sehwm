#!/bin/bash

echo " Starting Full System Update "
echo "1. Updating Pacman packages..."
sudo pacman -Syu --noconfirm

echo "2. Updating AUR packages..."
yay -Sua --noconfirm

echo " Update Complete! "
read -p "Press [Enter] to close..."
