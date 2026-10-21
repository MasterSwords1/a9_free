# Open RAM-run flash programmer. Same toolchain options as the top-level Makefile.

TOOLCHAIN ?= csdtk
CSDTK     ?= $(abspath $(dir $(lastword $(MAKEFILE_LIST)))../../../GPRS_C_SDK/CSDTK)

ifeq ($(TOOLCHAIN),csdtk)
ifeq ($(wildcard $(CSDTK)/bin/mips-elf-gcc),)
$(error CSDTK not found at $(CSDTK); set CSDTK=/path/to/CSDTK or use TOOLCHAIN=stock)
endif
CROSS := LD_LIBRARY_PATH=$(CSDTK)/lib:$(CSDTK)/lib32:$$LD_LIBRARY_PATH $(CSDTK)/bin/mips-elf-
ARCH  := -EL -march=xcpu -mtune=xcpu -Wa,-march=xcpu,-mtune=xcpu
else
CROSS ?= mipsel-elf-
ARCH  ?= -march=mips1
endif

CC      := $(CROSS)gcc
READELF := $(CROSS)readelf

CFLAGS  = -std=gnu99 $(ARCH) -O2 -nostdlib -nostartfiles -ffreestanding -mno-abicalls -fno-pic -G0
LDFLAGS = -Wl,-N -T open_ramrun.ld

all: open_ramrun_clean.elf

open_ramrun_clean.elf: entry.S open_ramrun.c open_ramrun.ld
	$(CC) $(CFLAGS) $(LDFLAGS) entry.S open_ramrun.c -o $@
	@echo "Build successful: $@"
	$(READELF) -l $@

clean:
	rm -f open_ramrun_clean.elf

.PHONY: all clean
