target   := nanoarch
sources  := nanoarch.c
CFLAGS   := -Wall -O2 -g
LDFLAGS  := -static-libgcc
LIBS     := -ldl
packages := gl glew glfw3 alsa

# do not edit from here onwards
objects := $(addprefix build/,$(sources:.c=.o))
ifneq ($(packages),)
    LIBS    += $(shell pkg-config --libs-only-l $(packages))
    LDFLAGS += $(shell pkg-config --libs-only-L --libs-only-other $(packages))
    CFLAGS  += $(shell pkg-config --cflags $(packages))
endif

.PHONY: all clean

all: $(target)
clean:
	-rm -rf build
	-rm -f $(target)
	-rm -f *.so rom-test*

$(target): Makefile $(objects)
	$(CC) $(LDFLAGS) -o $@ $(objects) $(LIBS)

build/%.o: %.c Makefile
	-mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -MMD -o $@ $<

-include $(addprefix build/,$(sources:.c=.d))

libretro-update:
	wget -q -O libretro.h https://raw.githubusercontent.com/libretro/RetroArch/master/libretro-common/include/libretro.h

CORE_NAME=snes9x
core:
	wget -q -O tmp.zip https://buildbot.libretro.com/nightly/linux/x86_64/latest/${CORE_NAME}_libretro.so.zip && \
	unzip -jo tmp.zip && rm tmp.zip

rom:
	wget -q -O rom-test.sfc "https://buildbot.libretro.com/assets/cores/Nintendo - Super Nintendo Entertainment System/240pSuite.sfc"

test:
	./nanoarch ./snes9x_libretro.so rom-test.sfc