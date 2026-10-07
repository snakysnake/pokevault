#!/bin/sh
# Show whether sprites and songs are present, then help with the one you pick.
set -eu
cd "$(dirname "$0")"

song_a=nitro/music/lake.wav
song_b=nitro/music/eterna_forest.wav

if [ -n "${1:-}" ] && [ "$1" != "sprites" ] && [ "$1" != "songs" ]; then
    echo "usage: ./install.sh [sprites|songs]" >&2
    exit 1
fi

have() {
    [ -s "$1" ]
}

song_count() {
    n=0
    if have "$song_a"; then
        n=$((n + 1))
    fi
    if have "$song_b"; then
        n=$((n + 1))
    fi
    echo "$n"
}

mark() {
    if have "$1"; then
        echo ready
    else
        echo missing
    fi
}

show_status() {
    if [ -t 1 ]; then
        printf '\033[H\033[2J'
    fi
    echo "PokeVault"
    echo
    printf '  %-18s %s\n' sprites "$(mark nitro/sprites.bin)"
    n=$(song_count)
    if [ "$n" -eq 2 ]; then
        printf '  %-18s %s\n' songs ready
    elif [ "$n" -eq 0 ]; then
        printf '  %-18s %s\n' songs missing
    else
        printf '  %-18s %s of 2\n' songs "$n"
        printf '  %-18s %s\n' lake.wav "$(mark "$song_a")"
        printf '  %-18s %s\n' eterna_forest.wav "$(mark "$song_b")"
    fi
    echo
}

pause() {
    if [ -t 0 ]; then
        printf '  press enter\n'
        read -r _ || true
    fi
}

install_sprites() {
    echo
    echo "  Sprites"
    echo
    echo "  Black and White sprites, saved as nitro/sprites.bin."
    echo "  Needs python3 and a network connection. Not kept in git."
    echo
    if have nitro/sprites.bin; then
        printf '  Already ready. Download again? [y/N] '
    else
        printf '  Download now? [y/N] '
    fi
    if [ ! -t 0 ]; then
        echo
        echo "  Run ./install.sh and choose sprites to download."
        return
    fi
    read -r answer || true
    case "$answer" in
        y|Y|yes|YES) ;;
        *) return ;;
    esac
    if ! command -v python3 >/dev/null 2>&1; then
        echo "  python3 is required." >&2
        pause
        return
    fi
    if [ ! -x tools/.pydeps/bin/python ]; then
        python3 -m venv tools/.pydeps
    fi
    if ! tools/.pydeps/bin/python -c "import PIL" >/dev/null 2>&1; then
        tools/.pydeps/bin/pip install Pillow
    fi
    echo
    if ! tools/.pydeps/bin/python tools/build_assets.py --sprites; then
        echo "  Sprite download failed." >&2
        pause
    fi
}

help_songs() {
    cat <<'EOF'

  Songs

  Pick any two songs you already have. Nothing is downloaded.
  The player reads 16-bit mono WAV at 22050 Hz, under these names:

    nitro/music/lake.wav
    nitro/music/eterna_forest.wav

  macOS includes afconvert:

    mkdir -p nitro/music
    afconvert -f WAVE -d LEI16@22050 -c 1 "/path/to/first song" nitro/music/lake.wav
    afconvert -f WAVE -d LEI16@22050 -c 1 "/path/to/second song" nitro/music/eterna_forest.wav

  The same conversion with ffmpeg:

    mkdir -p nitro/music
    ffmpeg -y -i "/path/to/first song" -ac 1 -ar 22050 -c:a pcm_s16le nitro/music/lake.wav
    ffmpeg -y -i "/path/to/second song" -ac 1 -ar 22050 -c:a pcm_s16le nitro/music/eterna_forest.wav

  Then run make. The songs are packed into pokevault.nds, which is the file
  you copy to the SD card. pokevault-nomusic.nds leaves them out.

EOF
    pause
}

case "${1:-}" in
    sprites)
        show_status
        install_sprites
        exit 0
        ;;
    songs)
        show_status
        help_songs
        exit 0
        ;;
esac

if [ ! -t 0 ]; then
    show_status
    exit 0
fi

while true; do
    show_status
    echo "  1 sprites    2 songs    q quit"
    printf '  > '
    if ! read -r choice; then
        echo
        exit 0
    fi
    case "$choice" in
        1|sprites) install_sprites ;;
        2|songs) help_songs ;;
        q|Q) exit 0 ;;
        *) ;;
    esac
done
