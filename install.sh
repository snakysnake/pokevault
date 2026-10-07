#!/bin/sh
# Fetch the sprite bank, then explain how to convert two songs of your own.
set -eu
cd "$(dirname "$0")"

force=0
if [ "${1:-}" = "--force" ]; then
    force=1
elif [ -n "${1:-}" ]; then
    echo "usage: ./install.sh [--force]" >&2
    exit 1
fi

sprites_ok=0
if [ "$force" -eq 0 ] && [ -s nitro/sprites.bin ]; then
    echo "nitro/sprites.bin is already present."
    echo "Run ./install.sh --force to download the sprites again."
    sprites_ok=1
else
    if ! command -v python3 >/dev/null 2>&1; then
        echo "python3 is required to build the sprite bank." >&2
        exit 1
    fi
    if [ ! -x tools/.pydeps/bin/python ]; then
        python3 -m venv tools/.pydeps
    fi
    if ! tools/.pydeps/bin/python -c "import PIL" >/dev/null 2>&1; then
        tools/.pydeps/bin/pip install Pillow
    fi
    echo "Downloading Black and White sprites into nitro/sprites.bin."
    if tools/.pydeps/bin/python tools/build_assets.py --sprites; then
        sprites_ok=1
    else
        echo "Sprite download failed." >&2
    fi
fi

cat <<'EOF'

Songs

Pick any two songs you already have. This script does not download music.
The player reads 16-bit mono WAV at 22050 Hz, and only these two names:

  nitro/music/lake.wav
  nitro/music/eterna_forest.wav

macOS includes afconvert, so nothing else needs to be installed:

  mkdir -p nitro/music
  afconvert -f WAVE -d LEI16@22050 -c 1 "/path/to/first song" nitro/music/lake.wav
  afconvert -f WAVE -d LEI16@22050 -c 1 "/path/to/second song" nitro/music/eterna_forest.wav

The same conversion with ffmpeg:

  mkdir -p nitro/music
  ffmpeg -y -i "/path/to/first song" -ac 1 -ar 22050 -c:a pcm_s16le nitro/music/lake.wav
  ffmpeg -y -i "/path/to/second song" -ac 1 -ar 22050 -c:a pcm_s16le nitro/music/eterna_forest.wav

Then run make. That packs the songs into pokevault.nds. Copy that file to the
SD card. The wav files stay on this computer. make also writes
pokevault-nomusic.nds, which leaves the songs out.
EOF

if [ "$sprites_ok" -ne 1 ]; then
    exit 1
fi
