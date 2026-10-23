#!/bin/sh
# Make src/prompts_svg.h: the controller button icons PS64's hints show ({cross},
# {l1}, ...), from Kenney's "Input Prompts" pack (CC0, www.kenney.nl), PlayStation
# set, as SVG text the app rasterises with nanosvg. Run again to change the set.
set -eu
cd "$(dirname "$0")/.."

URL=https://kenney.nl/media/pages/assets/input-prompts/8de120163f-1783763952/kenney_input-prompts_1.5.zip
SHA256=ac2fcf599080b0f3ba2d174c9474db6df1a0e96ff0662580e2da79a122ab78a1

# token = file in "PlayStation Series/Vector" (order = the PROMPT_* enum in src/prompts.h)
SET="cross:playstation_button_color_cross
circle:playstation_button_color_circle
square:playstation_button_color_square
triangle:playstation_button_color_triangle
l1:playstation_trigger_l1
r1:playstation_trigger_r1
l2:playstation_trigger_l2
r2:playstation_trigger_r2
options:playstation5_button_options
dpad:playstation_dpad_all
lr:playstation_dpad_horizontal
ud:playstation_dpad_vertical
touch:playstation5_touchpad"

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
curl -fsSL -o "$tmp/prompts.zip" "$URL"
echo "$SHA256  $tmp/prompts.zip" | shasum -a 256 -c - >/dev/null
unzip -q -j "$tmp/prompts.zip" "PlayStation Series/Vector/*" -d "$tmp/svg"

out=src/prompts_svg.h
{
    echo "/* Controller button icons for PS64's hints: Kenney \"Input Prompts\" 1.5, PlayStation"
    echo " * set (CC0, www.kenney.nl). Made by tools/gen_prompts.sh - don't edit. */"
    echo "#ifndef PROMPTS_SVG_H"
    echo "#define PROMPTS_SVG_H"
    echo ""
    echo "static const char *const prompt_svgs[] = {"
    echo "$SET" | while IFS=: read -r token file; do
        printf '    /* {%s} */\n    "' "$token"
        # D-pad: Kenney marks the directions in red; here they're white and the others
        # fade to 30% (70% transparent).
        case $token in
        dpad|lr|ud) recolour='s/fill="#FFFFFF"/fill="#FFFFFF" fill-opacity="0.3"/g; s/#E73246/#FFFFFF/g' ;;
        *)          recolour='' ;;
        esac
        # one C string: newlines dropped, quotes escaped
        tr -d '\n\r' < "$tmp/svg/$file.svg" | sed "$recolour" |
            sed 's/\\/\\\\/g; s/"/\\"/g; s/  */ /g'
        printf '",\n'
    done
    echo "};"
    echo ""
    echo "#endif"
} > "$out"
echo "prompts: $out ($(echo "$SET" | wc -l | tr -d ' ') icons)"
