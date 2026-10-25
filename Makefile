#---------------------------------------------------------------------------------
# Micropolis for the Nintendo 3DS (devkitPro / libctru).
#   make            -> micropolis.3dsx (+ .smdh)
#   make cia        -> micropolis.cia (needs makerom + bannertool)
#---------------------------------------------------------------------------------
.SUFFIXES:

ifeq ($(strip $(DEVKITARM)),)
$(error "Please set DEVKITARM in your environment. export DEVKITARM=<path to>devkitARM")
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITARM)/3ds_rules

TARGET      := micropolis
BUILD       := build
SOURCES     := engine src src/3ds
DATA        :=
INCLUDES    := engine src assets
ROMFS       := romfs
APP_TITLE   := Micropolis
APP_DESCRIPTION := SimCity classic, dual screen + stereoscopic
APP_AUTHOR  := Micropolis / 3DS port
ICON        := 3ds/icon.png

ARCH    := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft

CFLAGS  := -g -Wall -O2 -mword-relocations -ffunction-sections -fdata-sections \
           $(ARCH) $(INCLUDE) -D__3DS__ -DARM11
CXXFLAGS := $(CFLAGS) -fno-rtti -fno-exceptions -std=gnu++11 -w
ASFLAGS := -g $(ARCH)
LDFLAGS  = -specs=3dsx.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map) -Wl,--gc-sections

LIBS    := -lctru -lm
LIBDIRS := $(CTRULIB)

#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))
export OUTPUT   := $(CURDIR)/$(TARGET)
export TOPDIR   := $(CURDIR)
export VPATH    := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) $(foreach dir,$(DATA),$(CURDIR)/$(dir))
export DEPSDIR  := $(CURDIR)/$(BUILD)

CPPFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
CFILES   := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))

export LD := $(CXX)
export OFILES_SOURCES := $(CPPFILES:.cpp=.o) $(CFILES:.c=.o)
export OFILES := $(OFILES_SOURCES)
export INCLUDE := $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
                  $(foreach dir,$(LIBDIRS),-I$(dir)/include) -I$(CURDIR)/$(BUILD)
export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)
export _3DSXDEPS := $(if $(NO_SMDH),,$(OUTPUT).smdh)
export APP_ICON := $(TOPDIR)/$(ICON)
export _3DSXFLAGS += --romfs=$(CURDIR)/$(ROMFS)
ifeq ($(strip $(NO_SMDH)),)
export _3DSXFLAGS += --smdh=$(CURDIR)/$(TARGET).smdh
endif

.PHONY: all clean cia romfs

all: romfs $(BUILD)
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

$(BUILD):
	@mkdir -p $@

romfs:
	@mkdir -p $(ROMFS)/res
	@cp -r res/* $(ROMFS)/res/

clean:
	@rm -fr $(BUILD) $(ROMFS) $(TARGET).3dsx $(OUTPUT).smdh $(TARGET).elf $(TARGET).cia

cia: all
	bannertool makebanner -i 3ds/banner.png -a 3ds/audio.wav -o $(BUILD)/banner.bnr || true
	bannertool makesmdh -s "$(APP_TITLE)" -l "$(APP_DESCRIPTION)" -p "$(APP_AUTHOR)" -i $(ICON) -o $(BUILD)/icon.icn
	makerom -f cia -o $(TARGET).cia -elf $(TARGET).elf -rsf 3ds/micropolis.rsf -icon $(BUILD)/icon.icn \
	        -target t -exefslogo -romfs-dir $(ROMFS) $(if $(wildcard $(BUILD)/banner.bnr),-banner $(BUILD)/banner.bnr,)

#---------------------------------------------------------------------------------
else
DEPENDS := $(OFILES:.o=.d)

$(OUTPUT).3dsx : $(OUTPUT).elf $(_3DSXDEPS)
$(OUTPUT).elf  : $(OFILES)

-include $(DEPENDS)
endif
