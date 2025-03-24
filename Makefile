
TARGET_STRING := wipeout-rewrite.elf
TARGET := $(TARGET_STRING)

# Preprocessor definitions
DEFINES := _FINALROM=1 NDEBUG=1 F3DEX_GBI_2=1

SRC_DIRS :=

# Whether to hide commands or not
VERBOSE ?= 1
ifeq ($(VERBOSE),0)
  V := @
endif

# Whether to colorize build messages
COLOR ?= 1

#==============================================================================#
# Target Executable and Sources                                                #
#==============================================================================#
# BUILD_DIR is the location where all build artifacts are placed
BUILD_DIR := build

# Directories containing source files
SRC_DIRS += src
SRC_DIRS += src/wipeout

C_FILES := $(foreach dir,$(SRC_DIRS),$(wildcard $(dir)/*.c))

# Object files
O_FILES := src/sndwav.o src/input.o src/mem.o src/platform_dc.o src/render_dc.o src/system.o src/types_dc.o src/utils.o src/wipeout/camera.o src/wipeout/droid.o src/wipeout/game.o src/wipeout/hud.o src/wipeout/image.o src/wipeout/ingame_menus.o src/wipeout/intro.o src/wipeout/main_menu.o src/wipeout/menu.o src/wipeout/object.o src/wipeout/particle.o src/wipeout/race.o src/wipeout/scene.o src/wipeout/sfx.o src/wipeout/ship_ai.o src/wipeout/ship.o src/wipeout/ship_player.o src/wipeout/title.o src/wipeout/track.o src/wipeout/ui.o src/wipeout/weapon.o

#-ggdb3
#CFLAGS = -fno-builtin -ffreestanding
#-Wall -Werror -Wno-implicit-fallthrough -Wformat=2 -DDCLOCALDEV -DOSDSHOWFPS

#
#-fanalyzer -Wextra

# tools
PRINT = printf

ifeq ($(COLOR),1)
NO_COL  := \033[0m
RED     := \033[0;31m
GREEN   := \033[0;32m
BLUE    := \033[0;34m
YELLOW  := \033[0;33m
BLINK   := \033[33;5m
endif

# Common build print status function
define print
  @$(PRINT) "$(GREEN)$(1) $(YELLOW)$(2)$(GREEN) -> $(BLUE)$(3)$(NO_COL)\n"
endef

#==============================================================================#
# Main Targets                                                                 #
#==============================================================================#

all: $(TARGET)

buildtarget:
	mkdir -p $(BUILD_DIR)

$(TARGET): $(O_FILES) | buildtarget
	kos-cc -o ${BUILD_DIR}/$@ $(O_FILES)

# array_fast_copy.o

clean:
	$(RM) wipeout-rewrite.cdi doom64.iso header.iso bootfile.bin $(O_FILES) $(BUILD_DIR)/$(TARGET)

cdi:
	@test -s ${BUILD_DIR}/${TARGET_STRING} || { echo "Please run make or copy release ${TARGET_STRING} to ${BUILD_DIR} dir before running make cdi . Exiting"; exit 1; }
	$(RM) wipeout-rewrite.cdi
	mkdcdisc -d wipeout -e $(BUILD_DIR)/$(TARGET) -o wipeout-rewrite.cdi -n "WIPEOUT" -N

dsiso:
	@test -s ${BUILD_DIR}/${TARGET_STRING} || { echo "Please run make or copy release ${TARGET_STRING} to ${BUILD_DIR} dir before running make dsiso . Exiting"; exit 1; }
	$(RM) doom64.iso
	mkdir -p ./tmp
	$(KOS_OBJCOPY) -R .stack -O binary $(BUILD_DIR)/$(TARGET) ./tmp/1ST_READ.BIN
	-cp -R selfboot/* tmp
	mkisofs -V "Doom 64" -G ip.bin -r -J -l -o doom64.iso ./tmp
	$(RM) ./tmp/1ST_READ.BIN
	$(RM) ./tmp/controls.ini
	$(RM) ./tmp/warn3.dt
	$(RM) ./tmp/doom1mn.lmp
	$(RM) ./tmp/symbols.raw
	$(RM) ./tmp/*.wad
	$(RM) ./tmp/mus/*
	$(RM) ./tmp/sfx/*
	$(RM) ./tmp/maps/*
	$(RM) ./tmp/tex/*
	rmdir ./tmp/mus
	rmdir ./tmp/sfx
	rmdir ./tmp/maps
	rmdir ./tmp/tex

dcload: $(TARGET)
	sudo ./dcload-ip/host-src/tool/dc-tool-ip -x $(BUILD_DIR)/$(TARGET) -c ./selfboot/

# -g

# -g

ALL_DIRS := $(BUILD_DIR) $(addprefix $(BUILD_DIR)/,$(SRC_DIRS))

print-% : ; $(info $* is a $(flavor $*) variable set to [$($*)]) @true

include ${KOS_BASE}/Makefile.rules
