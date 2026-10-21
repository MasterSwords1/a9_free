# =============================================================================
# a9_free: open-source firmware for the RDA8955 (Ai-Thinker A9 / A9G)
# =============================================================================
#
# Only the CSDTK *compiler* is used (mips-elf-gcc/objcopy/objdump); no SDK
# headers or vendor libraries. CSDTK's GCC 4.4 is the toolchain verified on
# hardware and the default:
#
#   make                                  # CSDTK at ../GPRS_C_SDK/CSDTK
#   make CSDTK=/path/to/CSDTK
#
# A stock bare-metal MIPS GCC also builds the firmware (the XCPU runs the MIPS I
# integer ISA; -march=xcpu and -march=mips1 emit the same instruction mix), but
# a GCC 15 build still hangs in some shell commands on hardware: EXPERIMENTAL.
#
#   make TOOLCHAIN=stock                  # mipsel-elf-gcc from PATH
#   make TOOLCHAIN=stock CROSS=mips-elf- ARCH="-EL -march=mips1"

TOOLCHAIN ?= csdtk
CSDTK     ?= $(abspath $(dir $(lastword $(MAKEFILE_LIST)))../GPRS_C_SDK/CSDTK)

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
OBJCOPY := $(CROSS)objcopy
OBJDUMP := $(CROSS)objdump

PORT    ?= /dev/ttyUSB0
BAUD    ?= 921600
FLASHER := python3 tools/a9g_flasher.py --port $(PORT) --baud $(BAUD) --ramrun tools/ramrun/open_ramrun_clean.elf

CFLAGS  := -std=gnu99 $(ARCH) -O2 -nostdlib -nostartfiles -ffreestanding -mno-abicalls -fno-pic -G0
CFLAGS  += -I. -Iport -Isrc -Ikernel/include -Ilibc

KERNEL_SRCS := kernel/tasks.c \
               kernel/list.c

APP_SRCS := start.S \
            port/port_asm.S \
            port/port.c \
            src/uart.c \
            src/uart2.c \
            src/nmea.c \
            src/pmu.c \
            src/gps.c \
            src/system.c \
            src/clock.c \
            src/rtc.c \
            src/flash.c \
            src/settings.c \
            src/gpadc.c \
            src/gpio.c \
            src/pwm.c \
            src/main.c

TARGET := a9_free

all: $(TARGET).bin

$(TARGET).elf: $(APP_SRCS) $(KERNEL_SRCS) linker.ld
	$(CC) $(CFLAGS) -Wl,-N -T linker.ld $(APP_SRCS) $(KERNEL_SRCS) -o $@

$(TARGET).bin: $(TARGET).elf
	$(OBJCOPY) -O binary $< $@
	@echo "=== Successfully Built: $@ ($$(wc -c < $@) bytes) ==="

disasm: $(TARGET).elf
	$(OBJDUMP) -d $< > $(TARGET).asm

flash: $(TARGET).bin
	$(FLASHER) --firmware $(TARGET).bin --address 0x08000000 --execute

monitor:
	$(FLASHER) --monitor --passive --monitor-time 20

shell:
	$(FLASHER) --shell --passive

clean:
	rm -f $(TARGET).elf $(TARGET).bin $(TARGET).asm

.PHONY: all clean flash monitor shell disasm
