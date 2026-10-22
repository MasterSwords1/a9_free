# a9_free: freeing the Ai-Thinker A9G from its proprietary firmware

The **Ai-Thinker A9 / A9G** is a cheap GSM/GPRS (+ GPS on the A9G) module built on the
**RDA8955** system-on-chip. It was programmable only through a closed vendor stack:

- the **GPRS C SDK**, where user code links against a prebuilt platform blob
  (`SW_V2130_csdk.elf`);
- **CSDTK**, an old MIPS GCC toolchain bundled with RDA's `cooltools`;
- **CoolWatcher / CoolHost**, Qt4 GUI tools that flash `.lod` files over a proprietary
  serial protocol.

The SDK has not been updated since 2020. The official GitHub organisation and its
documentation site are gone. The tooling no longer runs comfortably on a modern Linux
machine. In practice, the hardware was e-waste.

**The goal of this project** is to understand the vendor firmware and tooling well
enough to replace them with open-source code: our own flasher, our own boot code, a
mainstream RTOS (FreeRTOS) and our own drivers. Once nothing proprietary is needed to
build, flash or run the board, it can host modern features again.

This repository holds the result so far: an open flasher and flash programmer, a
FreeRTOS port, and a growing set of reverse-engineered hardware drivers. Every register
sequence was taken from the vendor firmware and the chip's own register map and then
checked on real hardware. Anything that has not been physically verified is marked.

> Status: the board boots, runs and is flashed with **no vendor code at all** (only the
> chip's mask ROM, which cannot be replaced). The core platform drivers work. The GSM
> modem, networking and most external peripherals are **not implemented yet**; see
> [What is not implemented yet](#what-is-not-implemented-yet). Drivers are still under
> active development.

---

## Contents

1. [Hardware and available resources](#hardware-and-available-resources)
2. [How we got here: the process](#how-we-got-here-the-process)
3. [Tools](#tools)
4. [What is implemented](#what-is-implemented)
5. [What is not implemented yet](#what-is-not-implemented-yet)
6. [Quick start](#quick-start)
7. [Architecture and memory map](#architecture-and-memory-map)
8. [Project structure](#project-structure)
9. [License and acknowledgements](#license-and-acknowledgements)

---

## Hardware and available resources

| Item | Details |
| :--- | :--- |
| SoC | RDA8955: XCPU (MIPS, R3000-style CP0, MIPS16e-capable) at up to 312 MHz, a second BCPU for the GSM baseband, 64 KB internal SRAM, 4 MB on-package SPI NOR flash (GD25Q32, JEDEC `0x001660C8`), 4 MB PSRAM, mask ROM at `0x81E00000` |
| Board | Ai-Thinker A9G "Pudding" development board: CP2102 USB-UART on the debug port (`HST_TX/HST_RX`), blue and green LEDs on GPIO 27/28, GK9501 GPS (A9G) on UART2 |
| Debug port | The chip's debug host answers a binary protocol (SXS) on `HST_RX/HST_TX` at 921 600 baud. Through it the host can read and write memory, reset the CPU and talk to the mask-ROM monitor. Everything here, including flashing, goes through this one serial line. |

### Reference material

| Resource | Where |
| :--- | :--- |
| GPRS C SDK (complete history, all branches and tags; the official repo is deleted) | **[MasterSwords1/GPRS_C_SDK](https://github.com/MasterSwords1/GPRS_C_SDK)**, an unofficial archive published as part of this project |
| Older partial copies of the SDK | e.g. [cong1278/GPRS_C_SDK_A9G](https://github.com/cong1278/GPRS_C_SDK_A9G), [zerosigu/GPRS_C_SDK](https://github.com/zerosigu/GPRS_C_SDK) (snapshots without the full history) |
| Original SDK documentation | was at `ai-thinker-open.github.io/GPRS_C_SDK_DOC`, now offline; board pinouts survive in the SDK's `doc/assets/` |
| Vendor site | [ai-thinker.com](https://www.ai-thinker.com/) (module pages and datasheets) |
| Chip register map | `8955_hard.xml` in CSDTK (`cooltools/chipgen/Modem2G/toolpool/map/8809/`): every module, register and bit of the RDA8955, plus the PMU ("opal") |
| Mask ROM | `platform/chip/rom/8955/lib/mem_bridge_rom_CHIP.elf` in the SDK: boot code, ROM monitor, ROM IRQ handler |
| Vendor platform firmware | `platform/csdk/*/SW_V2130_csdk.elf` and the per-demo `hex/*/…_BASE_csdk_debug.elf` (with symbols): the HAL, PMD (power), calibration and OS code all drivers here were traced from |
| CSDTK toolchain | was a separate download, not part of the SDK repo; only its `mips-elf-gcc`, `objdump` and `objcopy` are used to build `a9_free` |

---

## How we got here: the process

The work happened in roughly ten phases, spread over several agent sessions (Codex,
Antigravity/Gemini, Claude Code) and directed by hand. Each phase built on what the
previous one proved on hardware. Dead ends are included because they shaped the
approach.

### 1. Studying the SDK

We mapped the SDK's structure first. User applications never touch the hardware: they
call into the closed platform blob through `g_InterfaceVtbl`, a table of 200+ function
pointers. The app sits in a separate flash partition (`0x240000`) with an init header
starting `0x87654321`. The linker scripts and `memd.def` gave the flash layout (vendor
platform `0x000000–0x240000`, user ROM `0x240000–0x340000`, RF calibration and factory
data at `0x3FA000–0x400000`).

### 2. First flashing attempts (dead end)

The first goal was a flasher that could load a UART demo and prove itself by catching
the demo's messages.
- **USB was a dead end.** Early attempts sniffed USB traffic in Docker (usbmon,
  tshark) and wrote PyUSB flashers. They never got a response: the "USB" traffic was
  only the CP2102 bridge.
- **A native CoolHost client got close.** It drove the vendor's
  `libcoolhost_library.so` through ctypes. This took udev rules and serial-group
  permissions (the device is owned by `uucp`), Qt and mount-namespace workarounds, and
  fixes to the COM-port linking.
- **But every first memory read failed** with CoolHost status 13.

### 3. Making the vendor tool work, then reading what it does

The native-client attempt was stuck on the same CoolHost errors. What finally worked
was reading the vendor tools directly instead of driving them blind, through plain
disassembly and the tools' own sources:
- **CoolWatcher's Ruby plugins and type maps** inside its AppImage (`fastpf.rb`,
  `target_executor.rb`, `8955_soft_pkg.xmd`) define the FastPF flash protocol and the
  address of the flash programmer's control table.
- **Disassembling CoolHost** (x86, `objdump`/`nm`: `CHDebugHostEngine::openDevice`,
  `CHConnection::genericRead`, the packet parser) showed it opens its serial port as
  `comport/COM1` *relative to its working directory*. With that link in place, plus
  an X display and clients on TCP port `7700 + COM index`, the "status 13" error
  (`QSerialPort::NotOpenError`) went away.
- **Disassembling the mask ROM** (`boot_UartMonitor`) together with the `Boot_Mode`
  bits in the register map showed that the ROM's UART monitor only runs when
  **Reset_Cause boot-mode bit 2** is set. The scripts had been setting only bits 1
  and 3.
- **CoolHost register arguments** are word indices, not byte offsets.
- **The FastPF protocol** works like this: a RAM-run programmer is loaded into SRAM,
  then commands go through two alternating descriptor slots (`0xA1C05F34` → ack
  `0xF0`, `0xA1C05F48` → ack `0xF1`). Program is command 1, erase 11, end 4,
  restart 7. The checksum is CRC-24, packed big-endian (found by disassembling
  `CR_LODPacket_rehash` in `libcoolruby.so`).
- **First full flash:** all 52 blocks of a vendor UART demo were flashed and verified.
  Its output was proven by reading the trace buffer out of target RAM. The message
  goes to UART1, not to the debug port.

### 4. Replacing the vendor pieces one by one

- **No more `.lod`:** the flasher now loads plain ELF/BIN images, and the build is an
  ordinary Makefile.
- **No more CoolHost:** `tools/a9g_flasher.py` speaks the SXS protocol directly with
  pyserial. A frame is `0xAD`, then a big-endian 16-bit length, flow `0xFF`, command,
  payload and an XOR checksum. `0x11`, `0x13` and `0x5C` are escaped as `0x5C, ~byte`.
  The commands are write-internal `0x84`, write32 `0x82`, write-block `0x83` and read32
  `0x02`.
- **No more vendor RAM-run:** `tools/ramrun/open_ramrun.c` is our own flash
  programmer. It runs in SRAM, drives the SPI flash controller (`0xA7FFF000`) itself,
  and speaks FastPF.

### 5. Our own firmware on the chip

- **Bare metal:** a bare-metal cooperative kernel came first. It showed the boot
  header magic `0xD9EF0045` and how to jump to code through the ROM monitor's executor.
- **FreeRTOS:** the upstream kernel followed. The vendor's memory model (fixed-size
  cluster pools, explicit task stacks) pointed to fully **static allocation**. The
  context frame layout came from the ROM's own IRQ save/restore code.
- **The LEDs needed pad routing:** GPIO 27/28 sit on the `KEYIN_2/3` pads, which must
  be muxed to their GPIO function before the pins respond.
- **Persistent boot came from the ROM:** on a cold boot the ROM checks
  `*(0x88000000) == 0xD9EF0045` and jumps to `0x88000010`. Relinking the firmware to
  sector 0 gave the first **persistent, standalone boot** of open firmware. That was
  confirmed by watching the LED survive a power cycle. This repository was created at
  this point.

### 6. Making it a real platform

Each item below was found by disassembling the vendor code, reading the register map,
and measuring on the board:
- **The flash programmer could not read.** Every SPI flash read returned 0 (the read
  flag is bit 16, and the data is MSB-aligned in `READ_BACK`). So busy-waits never
  waited, and every second 4 KB sector silently failed to erase. Firmware fetched from
  those sectors crashed. Rewritten to the vendor `hal_SpiFlash*` protocol (256-byte page
  programs, address in `CMD_ADDR[31:8]`), the programmer now verifies byte for byte and
  is about 4× faster (2.4 MB in 93 s).
- **Interrupts never fired.** Reading the IRQ controller's `SC` register is a
  semaphore that *disables* interrupts. The exception vector with BEV=0 is
  `0x81C00200`, which we found by rebasing the `DRAM_HANDLER` constant from the
  register map. The old vector stubs had been overwriting ROM data.
- **Random stalls came from the context switch.** It was not atomic, and it lost each
  task's interrupt state. It was rewritten around one 128-byte frame with per-task CP0
  Status and critical-section nesting, using R3000 `rfe` semantics.
- **A shell needed a different input path.** The debug host owns the UART's receive
  side, so console input arrives through an SRAM mailbox that the host writes over
  SXS. Output is plain serial text between SXS frames.
- **Recovery without a power cycle:** a **serial break** resets the debug host's
  receive state. It recovers a hung board without unplugging it.
- **The rest of the drivers** followed the same method, one at a time: watchdog and
  reset cause, PLL, RTC, runtime flash storage, GPADC with eFuse calibration, GPIO
  interrupts, PWM, and idle sleep. See [What is implemented](#what-is-implemented).

### 7. GPS: an open question

The full vendor power-on sequence for the GPS is implemented and verified up to the
UART: UART2 at 9600 baud, the VIBR LDO, and a 32 kHz clock output. NMEA injected
through UART2's internal loopback parses on the target. The receiver on the test board
has never transmitted, so the cause is probably on the board itself. See
[GPS_NOTES.md](GPS_NOTES.md).

### Lessons

- **Use evidence, not guesses.** Read the disassembly or the register map before
  writing a register. Most early time went into confident guesses, and every
  breakthrough came from reading the vendor code.
- **Separate "claimed" from "verified".** Several early "successes" were only telemetry
  the firmware reported about itself. The milestone table below keeps that distinction.

---

## Tools

### Used for reverse engineering

| Tool | Role |
| :--- | :--- |
| `objdump` / `nm` (host binutils) | Disassembly of CoolHost and its libraries (x86), the source of the status-13 and protocol fixes |
| CSDTK `mips-elf-objdump` / `readelf` / `nm` | Disassembly (MIPS32 and MIPS16e), symbol and literal-pool reads of the vendor firmware |
| `8955_hard.xml` | The chip's register map, used for every register name, offset and bit |
| Python 3 | Extraction scripts (literal pools, calibration data, LOD parsing), on-target test harnesses |
| Docker, usbmon, tshark | Early capture of the vendor tool's traffic (the USB dead end) |
| CoolWatcher AppImage (CoolHost, Ruby scripts) | Reference implementation of the protocols; no longer used |

### Built in this project

| Tool | What it does |
| :--- | :--- |
| `tools/a9g_flasher.py` | Pure-Python flasher, monitor and shell client over the debug port. It enters the ROM monitor (serial break + boot-mode reset, with retries), loads the RAM-run programmer, flashes BIN/ELF/LOD images (writing the boot magic last), runs code via the ROM executor, streams host events, gives an interactive shell (`--shell`) or scripted commands (`--send`), and sets the RTC from the PC (`--set-time`). It also stops the watchdog whenever it takes over the chip. |
| `tools/ramrun/open_ramrun.c` | Open SRAM flash programmer (FastPF protocol, vendor SPI protocol, ~3.7 KB) |
| Host tests (`tests/`) | Unit tests for target code that can run on a PC (the NMEA parser) |

---

## What is implemented

✅ verified on hardware · 🟡 implemented, not fully physically verified

| # | Milestone | Status |
| :--- | :--- | :--- |
| – | **Open toolchain**: flasher, RAM-run programmer, ELF/BIN/LOD loading, persistent boot | ✅ Flashes verify byte for byte. The board recovers from a hung bus with a serial break. |
| – | **FreeRTOS V11.1 port** for the XCPU | ✅ Static allocation. One 128-byte context frame shared by `vPortYield` and the IRQ path, per-task CP0 Status and critical nesting, `rfe` return. |
| **1** | **Preemptive tick** | ✅ OS timer IRQ via the BEV=0 vector at `0x81C00200`. The period is dithered between 163 and 164 counts of the 16 384 Hz timer, so it averages exactly 100 Hz (120.89 s of ticks against 120.89 s on the PC). |
| **2** | **Bidirectional serial console** | ✅ Output on the debug UART. Input goes through an SRAM mailbox written over SXS, because the debug host owns UART RX. Start it with `make shell`. |
| **3** | **Onboard GPS (GK9501)** | 🟡 UART2 driver, VIBR LDO and 32 kHz clock power-on (vendor `GPS_Open`), NMEA reader task and GGA/RMC parser are all verified via UART2 loopback. The receiver on the test board has never produced data; see [GPS_NOTES.md](GPS_NOTES.md). |
| **4** | **Watchdog and proper reset** | ✅ Vendor soft-reset sequence. The reset cause (power-on, soft, watchdog, host debug or RTC alarm) comes from the ROM's saved copy. A 4 s hardware watchdog is fed from the idle task (`wdt`; `hang` to test it). |
| **5** | **CPU clock / PLL** | ✅ The ROM leaves the CPU on the 26 MHz crystal (19.5 MIPS measured). The vendor PLL sequence now runs it at 250 MHz by default (about 166 MIPS). As in the vendor's `boot_SysSetupPLL`, the memory bridge and SPI flash move to the PLL with it, which roughly doubles flash read speed. The ISPI (PMU bus) divider follows the system clock. Before raising the clock, the core supply (PMU `0x4B` `Vbuck1_ldo_bit_nlp`) goes from the ROM's code 8 to the vendor's operating code 6 (smaller is higher voltage), as `pmd_SetVcoreToDefaultVolt` does. 312 MHz (about 206 MIPS) is opt-in with `cfg clock 312` (see Known issues). `clock`, `clock set <MHz>`. |
| **6** | **RTC / calendar** | ✅ Calendar block at `0xA1A0A000`. Encodings were verified on hardware: 1-based day and month, hardware leap years, ISO weekday 1–7. One-shot alarm through `SYS_IRQ_CALENDAR`. The time survives reboots and reflashes, but not a power-off (no battery). `date`, `alarm`; `--set-time` syncs it from the PC. |
| **7** | **Flash storage** | ✅ The firmware erases and programs flash while running from it, using SRAM-resident routines (`.ramfunc`) with interrupts off. Writes are confined to `0x380000–0x38FFFF`, so the firmware and the factory calibration can never be touched. A CRC-checked settings record (boot clock, watchdog, note) is applied at boot. `cfg …`, `flash test`. |
| **8** | **GPADC** | 🟡 8-channel 10-bit ADC. The per-chip two-point calibration is read from the PMU eFuse. Each read selects the fastest conversion interval, drops the stale result and averages 8. Channel 7 is VBAT. Not yet checked against a known voltage on an external pin; the channel-to-pin mapping is unconfirmed. `adc`. |
| **9** | **GPIO interrupts and PWM** | 🟡 GPIO drive, read and pad mux, and rising/falling/debounce/level interrupts on both banks, are all verified (20 edges give 20 interrupts on GPIO 1, 3, 30, 32 and 34). PWL1 PWM on `GPIO_2` is **not physically verified**: the LED pads have no PWM function, so it needs a `GPIO_2 → GPIO_3` jumper. `gpio`, `gpio irqtest`, `pwm`. |
| **10** | **Low-power sleep** | 🟡 The idle task gates the CPU clock (`Cpu_Sleep`, from SRAM, as the vendor `hal_LpsLightSleep` does). It wakes on the timer, GPIO or RTC, and sleeps about 80% of the time at idle with tick accuracy unchanged. The current draw has not been measured. `sleep`. |

Shell commands: `help`, `status`, `uptime`, `led blue|green on|off`, `peek`, `poke`,
`gps on|off`, `gps`, `gps raw`, `pmu <reg>`, `pmuw <reg> <val>`, `wdt [on [ms]|off]`,
`hang`, `clock`, `clock set <MHz>`, `u2baud`, `date [set …]`, `alarm [in <s>|off]`,
`cfg [clock|wdt|note <v>|save|erase]`, `flash [test]`, `adc [ch|cal]`,
`gpio <pin> [in|0|1]`, `gpio irqtest [pin]`, `pwm <0..255>|off`, `sleep [on|off]`, `reboot`.

---

## What is not implemented yet

Only the vendor firmware has these today. Drivers are still being developed, and items
that need extra hardware are marked.

| Area | Missing | Needs |
| :--- | :--- | :--- |
| **GSM / GPRS modem** | GSM protocol stack (L1–L3), the BCPU baseband/DSP code, RF transceiver driver, SIM, SMS, voice calls, GPRS data | GSM antenna and a SIM card; by far the largest piece of work |
| **Networking** | TCP/IP, TLS, MQTT/HTTP, the AT command interpreter | A working data link first; open stacks (e.g. lwIP) can be reused |
| **GPS fix** | Position fix on the test board (the receiver is silent; see [GPS_NOTES.md](GPS_NOTES.md)) | GPS antenna, and a check of the receiver's supply |
| **Peripherals** | UART1 (header pins, real hardware RX), I2C, SPI master, USB device, SD card (SDMMC), LCD and camera interfaces, audio codec/speaker/mic, keypad | A second USB-UART, or the matching device, for each |
| **Power** | Battery monitoring and charging control, deep sleep (PSRAM/flash power-down), PSRAM (external RAM) setup | A battery; a current meter to measure sleep |
| **System** | Filesystem, firmware over-the-air updates, DMA (IFC) driver | None (open filesystems such as littlefs can be reused) |
| **Verification gaps** | GPADC on a known voltage, PWM duty on a pin, sleep current | A jumper wire; a USB power meter |

### Known issues

- **312 MHz is opt-in.** The "memory corruption" seen earlier (statics such as the
  reset cause and GPADC calibration overwritten with SRAM addresses, hangs in
  `flash test`, watchdog reboots) turned out to be the CPU running at 312 MHz on the
  ROM's core voltage. With the same binary and only the saved boot clock changed,
  26 to 250 MHz ran clean while 312 MHz faulted on 6 of 6 boots. The firmware now
  sets the vendor's core voltage at boot (PMU `0x4B[11:8]` = 6, the highest level the
  vendor firmware uses). With it, 312 MHz passed 35 of 36 boots (one silent reboot)
  and a 4-minute soak. 250 MHz has never failed, so it stays the default; going past
  the vendor's voltage was not tried.
- Non-interrupt exceptions (address, bus, reserved-instruction errors) used to be
  treated as interrupts: the handler returned to the faulting instruction and looped,
  and each pass overwrote the current task's saved stack pointer, which turned a
  single fault into widespread corruption. They now stop the CPU and report
  `0xDEAD00<ExcCode>`, the EPC and Status as host events; the watchdog then resets.
- Builds made with a stock GCC 15 (`TOOLCHAIN=stock`) hung in some shell commands
  (`alarm in`, `flash test`, `adc`). Those runs were at 312 MHz and have not been
  repeated at 250 MHz; CSDTK builds are the reference.

Rough progress, weighted by effort to reach parity with the vendor firmware: tooling,
boot and OS 100%; core platform drivers about 85%; peripheral drivers about 10%; modem
and networking 0%. That is **about 25% overall**, and about 65% for using the board as
a general-purpose microcontroller.

---

## Quick start

### Requirements

- A MIPS cross compiler. a9_free uses no SDK headers or vendor libraries, only a
  compiler:
  - **CSDTK's `mips-elf-gcc`** (GCC 4.4 with RDA's `-march=xcpu`) is the toolchain
    verified on hardware and the default. The Makefile looks for it at
    `../GPRS_C_SDK/CSDTK`, or set `CSDTK=/path/to/CSDTK`.
  - **A stock bare-metal GCC** (`make TOOLCHAIN=stock`, `mipsel-elf-gcc` from PATH)
    is *experimental*. The XCPU runs the MIPS I integer ISA, and `-march=xcpu` and
    `-march=mips1` emit the same instruction mix. A GCC 15 build boots and runs,
    but still hangs in some shell commands on hardware.
- Python 3 with `pyserial` (and `pyelftools` for ELF images).
- An A9/A9G board on `/dev/ttyUSB0` through the debug port's USB-UART. Your user needs
  access to the tty; on some distributions it belongs to the `uucp` group, not
  `dialout`.

### Build, flash, talk to it

```bash
make                      # builds a9_free.bin
make flash                # enters the ROM monitor, loads the RAM-run programmer, flashes sector 0
make shell                # interactive shell (Ctrl+] to quit)
python3 tools/a9g_flasher.py --passive --send status --send date   # scripted commands
python3 tools/a9g_flasher.py --set-time --send date                # set the RTC from the PC
make -C tools/ramrun      # rebuild the flash programmer
```

The flasher also accepts vendor `.lod` images (`--firmware image.lod`). In that case
it writes the boot-sector magic last, as the vendor tools do. If the board stops
responding, the flasher's serial break usually recovers it. Otherwise unplug and replug
it (hold the power key if it does not start).

---

## Architecture and memory map

```
 Power-on ──► Mask ROM (0x81E00000)
              │  boot-mode bits in Reset_Cause set? ──► ROM monitor (SXS on the debug UART:
              │                                         memory access, executor, flashing)
              ▼
              *(0x88000000) == 0xD9EF0045 ? ──► jump to 0x88000010
              ▼
 start.S      interrupts off, install "j _freertos_irq_entry" at the BEV=0 vector 0x81C00200,
              copy .irqsram (handler at 0x81C00280) and .data/.ramfunc to SRAM, zero .bss
              ▼
 main()       reset cause, settings from flash, PLL to 250 MHz, LEDs, GPADC calibration,
              watchdog, tasks (heartbeat, worker, GPS reader, shell), scheduler
```

| Region | Physical | CPU address | Use |
| :--- | :--- | :--- | :--- |
| SPI flash, firmware | `0x000000` | `0x88000000` (cached), `0xA8000000` (uncached) | Boot magic, code, `.rodata`, load images of `.data`/`.ramfunc`/`.irqsram` |
| SPI flash, storage | `0x380000–0x38FFFF` | – | Settings record and test sector (the only area the firmware writes) |
| SPI flash, factory | `0x3FA000–0x3FFFFF` | – | RF calibration and factory data (read-only) |
| SRAM, ROM data | `0x01C00194–0x01C0027F` | `0x81C00194` | Owned by the ROM; the BEV=0 vector slot is at `0x81C00200` |
| SRAM, IRQ handler | `0x01C00280` | `0x81C00280` | `_freertos_irq_entry` |
| SRAM, data | `0x01C01000` | `0x81C01000` | `.ramfunc` (flash/sleep code), `.data`, `.bss`, task stacks |
| SRAM, shell mailbox | `0x01C07F00` | `0xA1C07F00` | Host-written console input ring |
| Peripherals | `0x01A00000` | `0xA1A00000` | sys_ctrl, IRQ, timers, GPIO, UARTs, ISPI (PMU), calendar, IOMUX |

---

## Project structure

```
a9_free/
├── Makefile, linker.ld, start.S, FreeRTOSConfig.h
├── port/                 FreeRTOS XCPU port (context switch, IRQ entry, tick)
├── kernel/               upstream FreeRTOS kernel (V11.1)
├── src/
│   ├── main.c            tasks and shell
│   ├── system.c          reset cause, soft reset, watchdog, idle sleep
│   ├── clock.c           PLL and system clock
│   ├── rtc.c             calendar / RTC and alarm
│   ├── flash.c           runtime SPI flash erase/program (SRAM-resident)
│   ├── settings.c        CRC-protected settings record
│   ├── pmu.c             PMU register access over ISPI
│   ├── gpadc.c           ADC with eFuse calibration
│   ├── gpio.c, pwm.c     GPIO + interrupts, PWL1
│   ├── uart.c            debug-UART console and SXS input mailbox
│   ├── uart2.c, gps.c    UART2 and GPS power/reader
│   └── nmea.c            NMEA parser
├── libc/                 minimal freestanding stdlib.h/string.h (for stock toolchains)
├── tests/                host-side unit tests
├── tools/
│   ├── a9g_flasher.py    flasher / monitor / shell client
│   └── ramrun/           open SRAM flash programmer
├── GPS_NOTES.md          GPS investigation notes
└── LICENSE               MIT
```

---

## License and acknowledgements

- a9_free (port, drivers, flasher, programmer): [MIT license](LICENSE).
- FreeRTOS kernel (`kernel/`): MIT license, © Amazon.com, Inc. or its affiliates.
- The GPRS C SDK and the vendor firmware are © Ai-Thinker / RDA and are not included
  here. They were studied to learn how the hardware works.
- Reverse engineered by disassembly (host and CSDTK binutils), the CoolWatcher scripts
  and the chip register map shipped with CSDTK.
