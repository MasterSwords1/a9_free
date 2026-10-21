# a9_free: Liberated FreeRTOS for RDA8955 (Ai-Thinker A9 / A9G)

**a9_free** is a 100% open-source, bare-metal upstream FreeRTOS firmware port and direct serial flashing toolchain for the **RDA8955** SoC (MIPS32r2 XCPU architecture), commonly found on the **Ai-Thinker A9** and **A9G (Pudding)** GSM/GPRS/GPS development boards.

---

## 🌟 Key Highlights

- **100% Open-Source & Blob-Free**: Completely eliminates **CoolHost**, **Wine**, **Qt4**, closed-source `SW_V2130_csdk.elf`, and proprietary `.lod` files.
- **Hardware Native Upstream FreeRTOS**: Official FreeRTOS kernel (`tasks.c` and `list.c`) with a reverse-engineered 120-byte MIPS XCPU context-switch frame complying with the MIPS O32 ABI.
- **Persistent Standalone Cold Boot**: Programs directly to SPI NOR Flash Sector 0 (`0x08000000`). Boots autonomously on power-on or USB plug-in without host debugger intervention.
- **Direct Serial Flasher (`tools/a9g_flasher.py`)**: Pure Python flasher with direct RAM-run injection and FastPF protocol over standard USB-UART (`/dev/ttyUSB0` at 921,600 baud).
- **Zero-Heap Static Allocation**: `configSUPPORT_DYNAMIC_ALLOCATION 0` avoids heap fragmentation in the internal 64 KB SRAM and keeps firmware footprint under **12 KB**.
- **Accurate Hardware Timing**: Monotonic 16,384 Hz hardware timer counter (`0xA1A02014`) delivers exact millisecond delays and 1 Hz LED blink rate.
- **Hardware IOMUX Routing**: Complete pad multiplexing enabling GPIO 27 (Blue LED), GPIO 28 (Green LED), and all user I/Os with maximum drive strength.

---

## 📐 Architecture & Memory Map

```
+-------------------------------------------------------------+
|                     RDA8955 Internal ROM                    |
|             (Hardwired Bootloader at 0x81E00000)             |
+-------------------------------------------------------------+
                               |
                               v (Cold Boot / Power On)
+-------------------------------------------------------------+
|           SPI NOR Flash Sector 0 (0x08000000)               |
|            Magic Tag: 0xD9EF0045                            |
|            FreeRTOS Boot Entry (0x88000010)                 |
|     - Validated by ROM before exiting monitor mode          |
|     - Initialises $sp = 0x81C07F00                          |
|     - Copies .data to cached SRAM (0x81C01000)              |
|     - Zeros .bss segment                                    |
|     - Jumps to C main()                                     |
+-------------------------------------------------------------+
                               |
                               v
+-------------------------------------------------------------+
|                     FreeRTOS Kernel Tasks                   |
|     - Task 1: Heartbeat (Blue LED Pin 27, 500 ms blink)     |
|     - Task 2: Worker    (Green LED Pin 28, 500 ms blink)    |
|     - Idle Task: Static allocation                          |
+-------------------------------------------------------------+
```

### Memory Map

| Region | Physical Address | Virtual (Cached KSEG0) | Size | Description |
| :--- | :--- | :--- | :--- | :--- |
| **SPI Flash (Boot & Code)** | `0x08000000` | `0x88000000` | 4 MB | Firmware binary with magic tag `0xD9EF0045`. Entry at `0x88000010`. |
| **Internal SRAM** | `0x01C00000` | `0x81C00000` | 64 KB | Stack (`0x81C07F00`), TCBs, stacks, and `.data` / `.bss` (`0x81C01000`). |
| **Hardware Peripherals** | `0x01A00000` | `0xA1A00000` (KSEG1) | — | System control, GPIO, Timers, UART, IOMUX. |

---

## 🛠️ Project Structure

```
a9_free/
├── Makefile                # Unified build and flash system
├── linker.ld               # MIPS linker script (Flash 0x88000000, SRAM 0x81C01000)
├── start.S                 # MIPS boot entry & hardware magic tag 0xD9EF0045
├── FreeRTOSConfig.h        # Minimal static FreeRTOS configuration
├── src/
│   └── main.c              # Application tasks, IOMUX init, and LED blinkers
├── port/
│   ├── port.c              # FreeRTOS port implementation (stack init, critical sections)
│   ├── port_asm.S          # 120-byte context switch & yield assembly
│   └── portmacro.h         # MIPS architecture port types & macros
├── kernel/                 # Upstream FreeRTOS kernel source
│   ├── tasks.c
│   ├── list.c
│   └── include/            # FreeRTOS headers (task.h, list.h, etc.)
└── tools/
    ├── a9g_flasher.py      # Standalone Python hardware flasher & monitor
    └── ramrun/             # Clean C flash programmer running in SRAM
        ├── open_ramrun.c
        ├── open_ramrun_clean.elf
        ├── open_ramrun.ld
        └── Makefile
```

---

## 🚀 Quick Start

### 1. Prerequisites
- **Toolchain**: `mips-elf-gcc` (bundled in CSDTK or standard MIPS bare-metal toolchain).
- **Host Tools**: Python 3 and `pyserial`:
  ```bash
  pip install pyserial
  ```

### 2. Build Firmware
```bash
make clean && make
```
Builds `a9_free.bin` (~11.4 KB).

### 3. Flash to Hardware
Connect your USB-to-UART adapter to the `HST_TX` and `HST_RX` pins on your A9/A9G board (`/dev/ttyUSB0`):
```bash
make flash
```
The flasher automatically:
1. Breaks into the RDA8955 ROM monitor over serial.
2. Injects the clean RAM-run programmer into internal SRAM.
3. Erases Sector 0 and writes `a9_free.bin` directly to `0x08000000`.
4. Triggers hardware soft reset into standalone execution.

### 4. Monitor Live Telemetry
To passively monitor live host trace events without resetting the device:
```bash
make monitor
```

---

## 🗺️ Roadmap & Status

| Step | Milestone | Status |
| :--- | :--- | :--- |
| **1** | **Preemptive timer interrupt** | ✅ Done: OS timer IRQ (100 Hz) via the BEV=0 vector at `0x81C00200`, unified 128-byte context frame with per-task Status and critical nesting. |
| **2** | **Bidirectional serial console** | ✅ Done: output on the debug UART, input through an SRAM mailbox written over SXS (the debug host owns UART RX). `make shell`. |
| **3** | **Onboard GPS (GK9501)** | 🟡 Firmware done and verified via UART2 loopback; the receiver on the test board has not produced NMEA yet. See [GPS_NOTES.md](GPS_NOTES.md). |
| **4** | **Watchdog & proper reset** | Not started. Replace the guessed `reboot` registers with the vendor sequence; add reset-cause reporting and a watchdog that recovers a hung system. |
| **5** | **CPU clock / PLL** | Not started. Confirm the real core clock (boot default vs the 312 MHz `status` claims) and bring up the PLL as the vendor image does; measure against the 16 kHz hardware timer. |
| **6** | **RTC / calendar** | Not started. Wall-clock time and alarms on the 32 kHz domain; `date` shell command. |
| **7** | **Flash storage API** | Not started. Erase/write/read from the running firmware (reusing the verified SPI flash protocol) for persistent settings that survive a reboot. |
| **8** | **GPADC** | Not started. ADC driver verified on a pin tied to GND/3.3 V and on internal channels; the battery-voltage channel is the basis for later battery monitoring. |
| **9** | **GPIO interrupts & PWM** | Not started. Edge-triggered GPIO IRQs through the system IRQ controller; PWM brightness control. |
| **10** | **Low-power sleep** | Not started. `Cpu_Sleep` with timer/GPIO wake-up; the tick and shell must survive sleep cycles. |

### Later (needs extra hardware)

| Milestone | Needs |
| :--- | :--- |
| **UART1 driver** (header pins, real hardware RX) | A second USB-UART or a TX→RX jumper |
| **GPS position fix** | A GPS antenna, and resolving the silent receiver (see [GPS_NOTES.md](GPS_NOTES.md)) |
| **Battery monitoring & charging** | A battery |
| **GSM/GPRS modem** | A GSM antenna and a SIM card |
| **I2C master** | An I2C device to talk to |

### Shell

```bash
make shell                                           # interactive, Ctrl+] to quit
python3 tools/a9g_flasher.py --passive --send status # scripted
```

Commands: `help`, `status`, `uptime`, `led blue|green on|off`, `peek`, `poke`,
`gps on|off`, `gps`, `gps raw`, `pmu <reg>`, `pmuw <reg> <val>`, `reboot`.

### Recovering a hung target
The flasher sends a serial break before entering the ROM monitor, which recovers a
target whose bus hung without a power cycle.

## 📜 License & Acknowledgments

- **FreeRTOS Kernel**: MIT License.
- **Port & Flasher**: 100% Open Source.
- Reverse-engineered using **REA** (Reverse Engineer Anything) and vendor chip maps.
