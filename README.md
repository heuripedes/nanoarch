# nanoarch

nanoarch is a small libretro frontend (nanoarch.c has less than 1000 lines of
code) created for educational purposes. It only provides the required (video,
audio and basic input) features to run most non-libretro-gl cores and there's
no UI or configuration support.

## Building

Other than `make`, `pkg-config` and a working C99 or C++ compiler, you'll need
`alsa` and `glfw` development files installed.

## Running

    ./nanoarch <core> <uncompressed content>

## Testing

    make rom core CORE_NAME=snes9x
    ./nanoarch ./snes9x_libretro.so rom-test.sfc