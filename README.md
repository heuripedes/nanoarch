# nanoarch

nanoarch is a small libretro frontend (nanoarch.c has less than 1000 lines of
code) created for educational purposes. It only provides the required (video,
audio and basic input) features to run most non-libretro-gl cores and there's
no UI or configuration support.

## Building

Other than a working C99 compiler (nanoarch.c is compilable as C++ too), you'll
need alsa and glfw development files installed.

## Running

    ./nanoarch <core> <uncompressed content>

