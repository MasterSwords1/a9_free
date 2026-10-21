# =============================================================================
# a9_free: 100% Open-Source FreeRTOS Firmware for RDA8955 (Ai-Thinker A9 / A9G)
# =============================================================================

CSDTK_PATH ?= ../GPRS_C_SDK/CSDTK

CC      := LD_LIBRARY_PATH=$(CSDTK_PATH)/lib:$(CSDTK_PATH)/lib32:$$LD_LIBRARY_PATH $(CSDTK_PATH)/bin/mips-elf-gcc
OBJCOPY := $(CSDTK_PATH)/bin/mips-elf-objcopy
OBJDUMP := $(CSDTK_PATH)/bin/mips-elf-objdump

PORT    ?= /dev/ttyUSB0
BAUD    ?= 921600
FLASHER := python3 tools/a9g_flasher.py --port $(PORT) --baud $(BAUD) --ramrun tools/ramrun/open_ramrun_clean.elf

CFLAGS  := -std=gnu99 -EL -march=xcpu -mtune=xcpu -Wa,-march=xcpu,-mtune=xcpu -O2 -nostdlib -nostartfiles -ffreestanding -mno-abicalls -fno-pic -G0
CFLAGS  += -I. -Iport -Isrc -Ikernel/include

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
            src/main.c

TARGET := a9_free

all: $(TARGET).bin

$(TARGET).elf: $(APP_SRCS) $(KERNEL_SRCS) linker.ld
	$(CC) $(CFLAGS) -EL -Wl,-N -T linker.ld $(APP_SRCS) $(KERNEL_SRCS) -o $@

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
