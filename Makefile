# Minimal devkitARM / libctru Makefile for the Mii Plaza Probe
ifeq ($(strip $(DEVKITARM)),)
$(error "DEVKITARM is not set")
endif
DEVKITPRO ?= /opt/devkitpro
CTRULIB   := $(DEVKITPRO)/libctru
TARGET    := miiplaza-probe
CC        := $(DEVKITARM)/bin/arm-none-eabi-gcc
ARCH      := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft
CFLAGS    := -g -Wall -O2 -mword-relocations -ffunction-sections $(ARCH) \
             -I$(CTRULIB)/include -DARM11 -D__3DS__
LDFLAGS   := -specs=3dsx.specs -g $(ARCH) -Wl,-Map,$(TARGET).map \
             -L$(CTRULIB)/lib -lctru -lm

all: $(TARGET).elf

$(TARGET).elf: source/main.c
	$(CC) $(CFLAGS) source/main.c -o $@ $(LDFLAGS)

clean:
	rm -f $(TARGET).elf $(TARGET).map $(TARGET).3dsx $(TARGET).cia

.PHONY: all clean
