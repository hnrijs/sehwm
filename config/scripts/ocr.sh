#!/usr/bin/env bash

set -Eeuo pipefail

DEPS=(maim magick tesseract xclip)

die() { echo "Error: $*" >&2; exit 1; }

check_deps() {
  local missing_dependencies=()
  for dep in "${DEPS[@]}"; do command -v "$dep" >/dev/null 2>&1 || missing_dependencies+=("$dep"); done
  if (( ${#missing_dependencies[@]} > 0 )); then
    die "Missing dependencies: ${missing_dependencies[*]}"
  fi
}

check_deps

maim -s \
  | magick - -colorspace Gray -normalize -contrast-stretch 2% -sharpen 0x1.0 -resize 200% png:- \
  | tesseract - stdout -l eng --psm 6 \
  | xclip -selection clipboard \
  && notify-send "OCR Successful" "Text copied!" \
  || notify-send "OCR Error" "Failed to read text"
