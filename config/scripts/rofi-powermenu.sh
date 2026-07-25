#!/usr/bin/env bash

lastlogin="$(last $USER | head -n1 | tr -s ' ' | cut -d' ' -f5,6,7)"
uptime="$(uptime -p | sed -e 's/up //g;s/ minutes/m/g;s/ hours*,/h/g')"

hibernate=''
shutdown='󰐥'
reboot=''
lock='󰌾'
suspend='󰤄'
logout='󰍃'
yes=''
no='' 

rofi_cmd() {
	rofi -dmenu \
		-p "  $USER" \
		-mesg "  Uptime: $uptime" \
		-theme-str "imagebox { background-image: url(\"$HOME/Pictures/main.png\", height); }" \
		-config "$HOME/.config/rofi/rofi-powermenu-config.rasi"
}

confirm_cmd() {
	rofi -theme-str 'window {location: center; anchor: center; fullscreen: false; width: 350px; border-radius: 0px;}' \
		-theme-str 'mainbox {orientation: vertical; children: [ "message", "listview" ]; border-radius: 0px;}' \
		-theme-str 'listview {columns: 2; lines: 1; border: 0px;}' \
		-theme-str 'element {border-radius: 0px;}' \
		-theme-str 'element-text {horizontal-align: 0.5;}' \
		-theme-str 'textbox {horizontal-align: 0.5;}' \
		-dmenu \
		-p 'Confirmation' \
		-mesg 'Are you sure?' \
		-config "$HOME/.config/rofi/rofi-powermenu-config.rasi"
}

confirm_exit() {
	echo -e "$yes\n$no" | confirm_cmd
}

run_rofi() {
	echo -e "$lock\n$reboot\n$logout\n$suspend\n$shutdown\n$hibernate" | rofi_cmd
}

run_cmd() {
	selected="$(confirm_exit)"
	if [[ "$selected" == "$yes" ]]; then
		if [[ $1 == '--shutdown' ]]; then
			systemctl poweroff || loginctl poweroff
		elif [[ $1 == '--reboot' ]]; then
			systemctl reboot || loginctl reboot
		elif [[ $1 == '--hibernate' ]]; then
			systemctl hibernate
		elif [[ $1 == '--suspend' ]]; then
			amixer set Master mute
			systemctl suspend
		elif [[ $1 == '--logout' ]]; then
			i3-msg exit
		fi
	else
		exit 0
	fi
}

chosen="$(run_rofi)"
case ${chosen} in
    $shutdown)
		run_cmd --shutdown
        ;;
    $reboot)
		run_cmd --reboot
        ;;
    $hibernate)
		run_cmd --hibernate
        ;;
    $lock)
        i3lock -c 000000 || slock
		;;
    $suspend)
		run_cmd --suspend
        ;;
    $logout)
		run_cmd --logout
        ;;
esac
