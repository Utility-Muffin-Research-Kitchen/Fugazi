# Fugazi MLP1 cross-build. Runs inside the mlp1-toolchain container with the
# UMRK workspace mounted at /workspace (Fugazi, Catastrophe, Jawaka as siblings).
TC       := /opt/mlp1-toolchain
CC       := $(TC)/bin/aarch64-buildroot-linux-gnu-gcc
SYSROOT  := $(TC)/aarch64-buildroot-linux-gnu/sysroot
FUGAZI   ?= /workspace/Fugazi
CATA     ?= /workspace/Catastrophe
CJSON    ?= /workspace/Jawaka/third_party/cjson
OUT      := $(FUGAZI)/ports/mlp1/pak/bin/fugazi

CFLAGS  := -std=gnu11 -O2 -Wall -Wextra -Wno-unused-parameter -DPLATFORM_MLP1 \
           -I$(FUGAZI)/cmd/fugazi -I$(CATA)/include -I$(CJSON) \
           -I$(SYSROOT)/usr/include -I$(SYSROOT)/usr/include/SDL2
LDFLAGS := -L$(SYSROOT)/usr/lib -lSDL2 -lSDL2_ttf -lSDL2_image -lGLESv2 -lEGL -lm -lpthread

.PHONY: all clean
all:
	mkdir -p $(dir $(OUT))
	$(CC) $(CFLAGS) -o $(OUT) $(FUGAZI)/cmd/fugazi/main.c $(CJSON)/cJSON.c $(LDFLAGS)
clean:
	rm -f $(OUT)
