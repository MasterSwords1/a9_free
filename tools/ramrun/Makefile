CSDTK ?= ../GPRS_C_SDK/CSDTK
CC = $(CSDTK)/bin/mips-elf-gcc
OBJDUMP = $(CSDTK)/bin/mips-elf-objdump
READELF = $(CSDTK)/bin/mips-elf-readelf

CFLAGS = -std=gnu99 -EL -march=mips32r2 -O2 -nostdlib -nostartfiles -ffreestanding -mno-abicalls -fno-pic
LDFLAGS = -EL -Wl,-N -T open_ramrun.ld

all: open_ramrun_clean.elf

open_ramrun_clean.elf: entry.S open_ramrun.c open_ramrun.ld
	LD_LIBRARY_PATH=$(CSDTK)/lib:$(CSDTK)/lib32:$$LD_LIBRARY_PATH $(CC) $(CFLAGS) $(LDFLAGS) entry.S open_ramrun.c -o $@
	@echo "Build successful: $@"
	$(READELF) -l $@

clean:
	rm -f open_ramrun_clean.elf
