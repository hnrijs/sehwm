#!/bin/bash

current=$(powerprofilesctl get)

if [ "$current" == "power-saver" ]; then
    powerprofilesctl set balanced
    notify-send "Power Mode" "Balanced" -i battery-positive -a "System"
elif [ "$current" == "balanced" ]; then
    powerprofilesctl set performance
    notify-send "Power Mode" "Performance" -i battery-charging -a "System"
else
    powerprofilesctl set power-saver
    notify-send "Power Mode" "Power Saver" -i battery-low -a "System"
fi
