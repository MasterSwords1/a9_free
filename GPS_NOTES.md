# Milestone 3: GPS (GK9501) on the A9G

## Status
- **Firmware side done and verified on hardware**: UART2 driver, GPS power control,
  NMEA reader task, GGA/RMC parser, `gps` / `gps on|off` / `gps raw` shell commands.
  NMEA injected through the UART2 internal loopback is received by the task and parsed
  on-target (`Fix quality 1, sats 8 ... Lat 48.1173000 Lon 11.5166666 Alt 545 m`).
- **Receiver not yet heard on this board**: with the full vendor power-on sequence
  applied, UART2 (and UART1) show no receive activity at all, so the GK9501 is not
  transmitting. No antenna is fitted (that would only prevent a fix, not NMEA output).
  Unresolved; see "Open questions".

## Established (vendor image + 8955_hard.xml)
- GPS is on **UART2** (`0xA1A16000`), 9600 8N1 (vendor `GPS_Open` config word 7 = 0x2580).
- UART2 pins: pad `GPIO_4` (0xA1A2A0A0) fn 1 = RXD, pad `GPIO_5` (0xA1A2A0A4) fn 1 = TXD.
- Baud divider: `sys_ctrl.Cfg_Clk_Uart[2]` (0xA1A00070); baud = 52 MHz * Num / Denom / 4,
  9600 = 0x00152801 (vendor `hal_UartOpen` table).
- `GPS_Open` = `user_uart_open(2)` + `pmd_SetLevel(8, 1)` + `hal_Sys32kClkOut(1)`;
  `GPS_Close` is the exact mirror.
- `pmd_SetLevel(8)` -> OPAL LDO mask 0x0020 = **VIBR**: `pd_vibr_act` (PMU 03H bit 5),
  `pd_vibr_lp` (08H bit 5). The vendor PMU init also sets `vibr_enable` (02H bit 7);
  on a ROM-booted chip `pd_vibr_pon` (0DH bit 5) is set too, so `gps on` clears it.
- 32 kHz out: pad `KEYOUT_3` (0xA1A2A0D0) fn 5 = DBG_CLK, `Cfg_Clk_Out` (0xA1A00058) = 0x2F
  (ClkOut_Sel = clk_32k, enable).
- PMU access: ISPI at 0xA1A13000, word = reg<<16 | data (write) or reg<<16 | 0x82000000
  (read), RX level in status[28:24].

## Ruled out on this board
- Every LDO forced on in act and power-on banks; all GPIOs released to input.
- `$PGKC462` version query sent on UART2 TX: no reply.
- GPS UART on UART1 instead of UART2: no activity.
- The SDK's own GPS demo cannot be used for comparison: the vendor image stops during
  external RAM setup on this board (`mon_Event 0x66660021, F, 0, 0x001660C8`), which also
  suggests the SDK's board config (incl. the GPS power mapping) may not match this
  board revision.

## Open questions
- Is the GK9501 powered from something outside PMU control on this board revision
  (board-level switch/jumper, separate regulator)? Measure GPS VCC with a meter while
  `gps on` is active, or check the board's GPS_TX header pin with a USB-UART at 9600.
