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
| **4** | **Watchdog & proper reset** | ✅ Done: `reboot` uses the vendor soft-reset sequence; reset cause (power-on / soft / watchdog / host debug / RTC alarm) read from the ROM's saved copy and shown in the banner and `status`; 4 s hardware watchdog fed from the idle task (`wdt`, `hang` to test). The flasher stops the watchdog when it takes over. |
| **5** | **CPU clock / PLL** | ✅ Done: the ROM leaves the CPU on the 26 MHz crystal (measured 19.5 MIPS); the vendor PLL sequence now runs the system at 312 MHz (233.7 MIPS measured, 12x faster). SPI flash and memory bridge stay on the slow clock; the ISPI (PMU) divider follows the system clock; UART2 baud verified unaffected. `clock`, `clock set <MHz>`. |
| **6** | **RTC / calendar** | ✅ Done: calendar block at `0xA1A0A000` following the vendor `hal_TimRtc*`; encodings verified on hardware (1-based day/month, hardware leap years, ISO weekday 1-7). `date`, `date set`, one-shot `alarm in <s>` via `SYS_IRQ_CALENDAR`; time survives reboot and reflash (lost on power-off without a battery). `tools/a9g_flasher.py --set-time` syncs from the PC. |
| **7** | **Flash storage API** | ✅ Done: runtime erase/program from the XIP firmware via SRAM-resident routines (`.ramfunc`, interrupts off, no calls back into flash), same SPI protocol as the programmer; writes confined to the storage window `0x380000-0x38FFFF` (firmware and factory/RF calibration sectors untouchable). 4 KB erase ~40 ms, write ~20 ms. CRC-checked settings record (boot clock, watchdog, note) applied at boot; survives reboot and reflash. `cfg ...`, `flash`, `flash test`. |
| **8** | **GPADC** | 🟡 Done, not fully physically tested: 8-channel 10-bit GPADC following the vendor `hal_AnaGpadc*` / `pmd_Gpadc*`; channel select in PMU `misc_3BH`, result in `pwm.GPADC_DATA`; per-chip two-point calibration read from the PMU eFuse (raw 622 @ 1133 mV, 770 @ 1400 mV on the test board). Reads switch to the fastest conversion interval, discard the stale result and average 8. Ch7 = VBAT (1/3 divider): ~3.75 V on USB power without a battery. `adc`, `adc <ch>`, `adc cal`. Not yet verified against a known voltage on an external ADC pin (GND / 1.8 V jumper test); channel-to-pin mapping unconfirmed. |
| **9** | **GPIO interrupts & PWM** | 🟡 Done, not fully physically tested: GPIO verified on hardware: drive/read/mux (pads of GPIO 0-7 and 25-34), rising/falling/debounce/level interrupts on both banks via `SYS_IRQ_GPIO`, following the vendor `hal_Gpio*`; self test toggles a pin with both-edge IRQs (20 edges -> 20 interrupts on GPIO 1, 3, 30, 32, 34). PWM: PWL1 on pad GPIO_2 (the LED pads have no PWM function) following `hal_PwlSelLevel`, **not physically verified** (needs a GPIO_2 -> GPIO_3 jumper; `pwm <level>` then reports the sensed duty). `gpio`, `gpio irqtest`, `pwm`. |
| **10** | **Low-power sleep** | 🟡 Done, not fully physically tested: idle-task light sleep gating the XCPU clock (`sys_irq.Cpu_Sleep`, from SRAM with interrupts off, as the vendor `hal_LpsLightSleep`); wake sources OS timer, GPIO, RTC. CPU asleep ~82% of the time at idle; tick accuracy unchanged by sleep; the tick period is now dithered between 163 and 164 timer counts so it averages exactly 100 Hz (was 99.90 Hz: 120 s now measures 120.89 s ticks vs 120.89 s PC); GPIO IRQs, RTC alarm, watchdog and shell all keep working. Actual current draw not measured (no meter). `sleep [on|off]`. |

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
`gps on|off`, `gps`, `gps raw`, `pmu <reg>`, `pmuw <reg> <val>`, `wdt [on [ms]|off]`, `hang`, `clock`, `clock set <MHz>`, `u2baud`, `date [set ...]`, `alarm [in <s>|off]`, `cfg [clock|wdt|note <v>|save|erase]`, `flash [test]`, `adc [ch|cal]`, `gpio <pin> [in|0|1]`, `gpio irqtest [pin]`, `pwm <0..255>|off`, `sleep [on|off]`, `reboot`.

### Recovering a hung target
The flasher sends a serial break before entering the ROM monitor, which recovers a
target whose bus hung without a power cycle.

## 📜 License & Acknowledgments

- **FreeRTOS Kernel**: MIT License.
- **Port & Flasher**: 100% Open Source.
- Reverse-engineered using **REA** (Reverse Engineer Anything) and vendor chip maps.
