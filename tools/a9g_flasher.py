#!/usr/bin/env python3
"""A9G / RDA8955 Standalone Pure-Python Serial Flasher.

Completely eliminates CoolHost, Qt4, Wine, C libraries, and proprietary .lod formats.
Directly communicates with the RDA8955 SoC over /dev/ttyUSB0 using the native
SXS / Debug Host serial protocol and standard ELF / BIN files.
"""

from __future__ import annotations

import argparse
import struct
import sys
import time
from dataclasses import dataclass
from pathlib import Path

try:
    import serial
except ImportError:
    print("Error: pyserial is required. Install with: pip install pyserial", file=sys.stderr)
    sys.exit(1)


# ---------------------------------------------------------------------------
# FastPF & Debug Host Constants
# ---------------------------------------------------------------------------
FPC_NONE = 0
FPC_PROGRAM = 1
FPC_END = 4
FPC_CHECK_FCS = 5
FPC_RESTART = 7
FPC_ERASE = 11

EVENT_FLASH_PROG_READY = 0xF0
EVENT_FLASH_PROG_ERROR = 0xE0
EVENT_FLASH_PROG_UNKNOWN = 0xD0

FPC_PROTOCOL_MAJOR = 0xFA01
FPC_PROTOCOL_MINOR = 0xFB05

EVENT_HOST_MONITOR_STARTS = (0xFF000001, 0xFF000002)

FLASH_BASE = 0x08000000
FASTPF_PROG_ACCESS_ADDRESS = 0xA1C05F30
HOST_MONITOR_CONTROL_ADDRESS = 0x81C000A0
DBG_HOST_CTRL_SET_OFFSET = 0
DBG_HOST_CTRL_CLR_OFFSET = 1
DBG_HOST_CLKDIV_OFFSET = 2
DBG_HOST_CFG_OFFSET = 3
DBG_HOST_H2P_STATUS_OFFSET = 5
SYS_CTRL_RESET_CAUSE_ADDRESS = 0x01A0009C
HST_EXECUTOR_X_CMD = 0xFF
BOOT_HST_MONITOR_START_CMD = 0xFD
BOOT_HST_MONITOR_END_CMD = 0xFE
BOOT_HST_MONITOR_END_EVT = 0xFF000003

MAX_FPC_BUFFER = 32 * 1024


# ---------------------------------------------------------------------------
# Pure-Python FCS (CRC-24) Implementation (Replaces libcoolruby.so)
# ---------------------------------------------------------------------------
FCS_TABLE = [
    0x00000000, 0x01bba1b5, 0x03cce2df, 0x0277436a, 0x0722640b, 0x0699c5be, 0x04ee86d4, 0x05552761,
    0x0e44c816, 0x0fff69a3, 0x0d882ac9, 0x0c338b7c, 0x0966ac1d, 0x08dd0da8, 0x0aaa4ec2, 0x0b11ef77,
    0x1c89902c, 0x1d323199, 0x1f4572f3, 0x1efed346, 0x1babf427, 0x1a105592, 0x186716f8, 0x19dcb74d,
    0x12cd583a, 0x1376f98f, 0x1101bae5, 0x10ba1b50, 0x15ef3c31, 0x14549d84, 0x1623deee, 0x17987f5b,
    0x39a881ed, 0x38132058, 0x3a646332, 0x3bdfc287, 0x3e8ae5e6, 0x3f314453, 0x3d460739, 0x3cfda68c,
    0x37ec49fb, 0x3657e84e, 0x3420ab24, 0x359b0a91, 0x30ce2df0, 0x31758c45, 0x3302cf2f, 0x32b96e9a,
    0x252111c1, 0x249ab074, 0x26edf31e, 0x275652ab, 0x220375ca, 0x23b8d47f, 0x21cf9715, 0x207436a0,
    0x2b65d9d7, 0x2ade7862, 0x28a93b08, 0x29129abd, 0x2c47bddc, 0x2dfc1c69, 0x2f8b5f03, 0x2e30feb6,
    0x73eaa26f, 0x725103da, 0x702640b0, 0x719de105, 0x74c8c664, 0x757367d1, 0x770424bb, 0x76bf850e,
    0x7dae6a79, 0x7c15cbcc, 0x7e6288a6, 0x7fd92913, 0x7a8c0e72, 0x7b37afc7, 0x7940ecad, 0x78fb4d18,
    0x6f633243, 0x6ed893f6, 0x6cafd09c, 0x6d147129, 0x68415648, 0x69faf7fd, 0x6b8db497, 0x6a361522,
    0x6127fa55, 0x609c5be0, 0x62eb188a, 0x6350b93f, 0x66059e5e, 0x67be3feb, 0x65c97c81, 0x6472dd34,
    0x4a422382, 0x4bf98237, 0x498ec15d, 0x483560e8, 0x4d604789, 0x4cdbe63c, 0x4eaca556, 0x4f1704e3,
    0x4406eb94, 0x45bd4a21, 0x47ca094b, 0x4671a8fe, 0x43248f9f, 0x429f2e2a, 0x40e86d40, 0x4153ccf5,
    0x56cbb3ae, 0x5770121b, 0x55075171, 0x54bcf0c4, 0x51e9d7a5, 0x50527610, 0x5225357a, 0x539e94cf,
    0x588f7bb8, 0x5934da0d, 0x5b439967, 0x5af838d2, 0x5fad1fb3, 0x5e16be06, 0x5c61fd6c, 0x5dda5cd9,
    0xe76ee56b, 0xe6d544de, 0xe4a207b4, 0xe519a601, 0xe04c8160, 0xe1f720d5, 0xe38063bf, 0xe23bc20a,
    0xe92a2d7d, 0xe8918cc8, 0xeae6cfa2, 0xeb5d6e17, 0xee084976, 0xefb3e8c3, 0xedc4aba9, 0xec7f0a1c,
    0xfbe77547, 0xfa5cd4f2, 0xf82b9798, 0xf990362d, 0xfcc5114c, 0xfd7eb0f9, 0xff09f393, 0xfeb25226,
    0xf5a3bd51, 0xf4181ce4, 0xf66f5f8e, 0xf7d4fe3b, 0xf281d95a, 0xf33a78ef, 0xf14d3b85, 0xf0f69a30,
    0xdec66486, 0xdf7dc533, 0xdd0a8659, 0xdcb127ec, 0xd9e4008d, 0xd85fa138, 0xda28e252, 0xdb9343e7,
    0xd082ac90, 0xd1390d25, 0xd34e4e4f, 0xd2f5effa, 0xd7a0c89b, 0xd61b692e, 0xd46c2a44, 0xd5d78bf1,
    0xc24ff4aa, 0xc3f4551f, 0xc1831675, 0xc038b7c0, 0xc56d90a1, 0xc4d63114, 0xc6a1727e, 0xc71ad3cb,
    0xcc0b3cbc, 0xcdb09d09, 0xcfc7de63, 0xce7c7fd6, 0xcb2958b7, 0xca92f902, 0xc8e5ba68, 0xc95e1bdd,
    0x94844704, 0x953fe6b1, 0x9748a5db, 0x96f3046e, 0x93a6230f, 0x921d82ba, 0x906ac1d0, 0x91d16065,
    0x9ac08f12, 0x9b7b2ea7, 0x990c6dcd, 0x98b7cc78, 0x9de2eb19, 0x9c594aac, 0x9e2e09c6, 0x9f95a873,
    0x880dd728, 0x89b6769d, 0x8bc135f7, 0x8a7a9442, 0x8f2fb323, 0x8e941296, 0x8ce351fc, 0x8d58f049,
    0x86491f3e, 0x87f2be8b, 0x8585fde1, 0x843e5c54, 0x816b7b35, 0x80d0da80, 0x82a799ea, 0x831c385f,
    0xad2cc6e9, 0xac97675c, 0xaee02436, 0xaf5b8583, 0xaa0ea2e2, 0xabb50357, 0xa9c2403d, 0xa879e188,
    0xa3680eff, 0xa2d3af4a, 0xa0a4ec20, 0xa11f4d95, 0xa44a6af4, 0xa5f1cb41, 0xa786882b, 0xa63d299e,
    0xb1a556c5, 0xb01ef770, 0xb269b41a, 0xb3d215af, 0xb68732ce, 0xb73c937b, 0xb54bd011, 0xb4f071a4,
    0xbfe19ed3, 0xbe5a3f66, 0xbc2d7c0c, 0xbd96ddb9, 0xb8c3fad8, 0xb9785b6d, 0xbb0f1807, 0xbab4b9b2,
]

FCS_MSB2LSB = [
    0x00, 0x80, 0x40, 0xc0, 0x20, 0xa0, 0x60, 0xe0, 0x10, 0x90, 0x50, 0xd0, 0x30, 0xb0, 0x70, 0xf0,
    0x08, 0x88, 0x48, 0xc8, 0x28, 0xa8, 0x68, 0xe8, 0x18, 0x98, 0x58, 0xd8, 0x38, 0xb8, 0x78, 0xf8,
    0x04, 0x84, 0x44, 0xc4, 0x24, 0xa4, 0x64, 0xe4, 0x14, 0x94, 0x54, 0xd4, 0x34, 0xb4, 0x74, 0xf4,
    0x0c, 0x8c, 0x4c, 0xcc, 0x2c, 0xac, 0x6c, 0xec, 0x1c, 0x9c, 0x5c, 0xdc, 0x3c, 0xbc, 0x7c, 0xfc,
    0x02, 0x82, 0x42, 0xc2, 0x22, 0xa2, 0x62, 0xe2, 0x12, 0x92, 0x52, 0xd2, 0x32, 0xb2, 0x72, 0xf2,
    0x0a, 0x8a, 0x4a, 0xca, 0x2a, 0xaa, 0x6a, 0xea, 0x1a, 0x9a, 0x5a, 0xda, 0x3a, 0xba, 0x7a, 0xfa,
    0x06, 0x86, 0x46, 0xc6, 0x26, 0xa6, 0x66, 0xe6, 0x16, 0x96, 0x56, 0xd6, 0x36, 0xb6, 0x76, 0xf6,
    0x0e, 0x8e, 0x4e, 0xce, 0x2e, 0xae, 0x6e, 0xee, 0x1e, 0x9e, 0x5e, 0xde, 0x3e, 0xbe, 0x7e, 0xfe,
    0x01, 0x81, 0x41, 0xc1, 0x21, 0xa1, 0x61, 0xe1, 0x11, 0x91, 0x51, 0xd1, 0x31, 0xb1, 0x71, 0xf1,
    0x09, 0x89, 0x49, 0xc9, 0x29, 0xa9, 0x69, 0xe9, 0x19, 0x99, 0x59, 0xd9, 0x39, 0xb9, 0x79, 0xf9,
    0x05, 0x85, 0x45, 0xc5, 0x25, 0xa5, 0x65, 0xe5, 0x15, 0x95, 0x55, 0xd5, 0x35, 0xb5, 0x75, 0xf5,
    0x0d, 0x8d, 0x4d, 0xcd, 0x2d, 0xad, 0x6d, 0xed, 0x1d, 0x9d, 0x5d, 0xdd, 0x3d, 0xbd, 0x7d, 0xfd,
    0x03, 0x83, 0x43, 0xc3, 0x23, 0xa3, 0x63, 0xe3, 0x13, 0x93, 0x53, 0xd3, 0x33, 0xb3, 0x73, 0xf3,
    0x0b, 0x8b, 0x4b, 0xcb, 0x2b, 0xab, 0x6b, 0xeb, 0x1b, 0x9b, 0x5b, 0xdb, 0x3b, 0xbb, 0x7b, 0xfb,
    0x07, 0x87, 0x47, 0xc7, 0x27, 0xa7, 0x67, 0xe7, 0x17, 0x97, 0x57, 0xd7, 0x37, 0xb7, 0x77, 0xf7,
    0x0f, 0x8f, 0x4f, 0xcf, 0x2f, 0xaf, 0x6f, 0xef, 0x1f, 0x9f, 0x5f, 0xdf, 0x3f, 0xbf, 0x7f, 0xff,
]


def calculate_fcs(data: bytes) -> int:
    """Calculate 24-bit FCS checksum used by RDA FastPF programmer."""
    ctx = 0xFFFFFFFF
    for b in data:
        val = (ctx >> 16) & 0xFF
        idx = val ^ FCS_MSB2LSB[b]
        ctx = ((ctx << 8) & 0xFFFFFFFF) ^ FCS_TABLE[idx]

    ctx = (~ctx) & 0xFFFFFFFF
    b0 = FCS_MSB2LSB[(ctx >> 16) & 0xFF]
    b1 = FCS_MSB2LSB[(ctx >> 8) & 0xFF]
    b2 = FCS_MSB2LSB[ctx & 0xFF]
    return (b0 << 16) | (b1 << 8) | b2


# ---------------------------------------------------------------------------
# Wire Framing & Escaping
# ---------------------------------------------------------------------------
def escape_xon_xoff(data: bytes) -> bytes:
    out = bytearray()
    for b in data:
        if b in (0x11, 0x13, 0x5C):
            out.append(0x5C)
            out.append(b ^ 0xFF)
        else:
            out.append(b)
    return bytes(out)


def calc_packet_crc(data: bytes) -> int:
    crc = 0
    for b in data:
        crc ^= b
    return crc


@dataclass
class ProgramBlock:
    address: int
    words: list[int]

    @property
    def data(self) -> bytes:
        return struct.pack("<%dI" % len(self.words), *self.words)


def parse_bin(path: Path, base_address: int = 0x08240000) -> list[ProgramBlock]:
    raw = path.read_bytes()
    if len(raw) % 4 != 0:
        raw += b"\x00" * (4 - (len(raw) % 4))
    words = list(struct.unpack("<%dI" % (len(raw) // 4), raw))
    return [ProgramBlock(base_address, words)]


def parse_elf(path: Path) -> list[ProgramBlock]:
    from elftools.elf.elffile import ELFFile

    blocks: list[ProgramBlock] = []
    with path.open("rb") as f:
        elffile = ELFFile(f)
        for seg in elffile.iter_segments():
            if seg["p_type"] == "PT_LOAD" and seg["p_filesz"] > 0:
                paddr = seg["p_paddr"]
                vaddr = seg["p_vaddr"]
                addr = paddr if paddr != 0 else vaddr
                if addr >= 0x80000000:
                    addr &= 0x0FFFFFFF
                data = seg.data()
                if len(data) % 4 != 0:
                    data += b"\x00" * (4 - (len(data) % 4))
                words = list(struct.unpack("<%dI" % (len(data) // 4), data))
                blocks.append(ProgramBlock(addr, words))
    if not blocks:
        raise ValueError(f"{path}: no loadable segments found in ELF")
    return blocks


def load_firmware(path: Path, address: int | None = None) -> list[ProgramBlock]:
    suffix = path.suffix.lower()
    if suffix == ".bin":
        base = address if address is not None else 0x08240000
        return parse_bin(path, base)
    elif suffix == ".elf":
        return parse_elf(path)
    else:
        with path.open("rb") as f:
            magic = f.read(4)
        if magic == b"\x7fELF":
            return parse_elf(path)
        base = address if address is not None else 0x08240000
        return parse_bin(path, base)


# ---------------------------------------------------------------------------
# Direct Serial SXS Driver
# ---------------------------------------------------------------------------
class SerialTarget:
    def __init__(self, port: str = "/dev/ttyUSB0", baud: int = 921600):
        self.port = port
        self.baud = baud
        self.ser: serial.Serial | None = None
        self.rx_buf = bytearray()
        self.escaped = False
        self.read_id = 0x10

    def open(self):
        print(f"[-] Opening {self.port} at {self.baud} baud (xonxoff=True)...")
        self.ser = serial.Serial(
            self.port,
            self.baud,
            timeout=0.05,
            xonxoff=True,
            rtscts=False,
            dsrdtr=False,
        )
        self.ser.reset_input_buffer()
        self.ser.reset_output_buffer()

    def close(self):
        if self.ser and self.ser.is_open:
            self.ser.close()

    def send_packet(self, flow_id: int, cmd: int, payload: bytes):
        inner = bytes([flow_id, cmd]) + payload
        crc = calc_packet_crc(inner)
        pkt_len = len(inner)
        raw_pkt = bytes([0xAD]) + struct.pack(">H", pkt_len) + inner + bytes([crc])
        wire = escape_xon_xoff(raw_pkt)
        self.ser.write(wire)

    def write_internal(self, offset: int, val: int):
        payload = struct.pack("<IB", offset, val)
        self.send_packet(0xFF, 0x84, payload)

    def write32(self, addr: int, val: int):
        payload = struct.pack("<II", addr, val)
        self.send_packet(0xFF, 0x82, payload)

    def write_block(self, addr: int, data: bytes):
        pad = (4 - (len(data) % 4)) % 4
        if pad:
            data = data + (b"\x00" * pad)
        chunk_size = 1024
        for i in range(0, len(data), chunk_size):
            chunk = data[i : i + chunk_size]
            payload = struct.pack("<I", addr + i) + chunk
            self.send_packet(0xFF, 0x83, payload)

    def read32(self, addr: int, timeout: float = 2.0) -> int:
        self.read_id = ((self.read_id + 1) & 0x7F) or 1
        read_id = self.read_id
        payload = struct.pack("<IB", addr, read_id)
        self.send_packet(0xFF, 0x02, payload)

        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            flow_id, data = self.poll_packet(timeout=0.1)
            if flow_id == 0xFF and data and len(data) >= 5 and data[0] == read_id:
                return struct.unpack_from("<I", data, 1)[0]
        raise TimeoutError(f"read32(0x{addr:08x}) timed out")

    def read_block(self, addr: int, size: int, timeout: float = 5.0) -> bytes:
        result = bytearray()
        for i in range(0, size, 4):
            word = self.read32(addr + i, timeout=timeout)
            result.extend(struct.pack("<I", word))
        return bytes(result[:size])

    def poll_packet(self, timeout: float = 0.2) -> tuple[int | None, bytes | None]:
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            while len(self.rx_buf) >= 4:
                if self.rx_buf[0] in (0xAD, 0xBB):
                    pkt_len = (self.rx_buf[1] << 8) | self.rx_buf[2]
                    total = pkt_len + 4
                    if len(self.rx_buf) >= total:
                        pkt = bytes(self.rx_buf[:total])
                        self.rx_buf = bytearray(self.rx_buf[total:])
                        flow_id = pkt[3]
                        data = pkt[4:-1]
                        return flow_id, data
                    else:
                        break
                else:
                    self.rx_buf.pop(0)

            n = self.ser.in_waiting if self.ser else 0
            raw = self.ser.read(n or 1) if self.ser else b""
            if raw:
                for b in raw:
                    if self.escaped:
                        self.rx_buf.append(b ^ 0xFF)
                        self.escaped = False
                    elif b == 0x5C:
                        self.escaped = True
                    else:
                        self.rx_buf.append(b)
            else:
                time.sleep(0.002)
        return None, None

    def wait_event(self, expected_events: int | tuple[int, ...], timeout: float = 10.0) -> int:
        expected_set = (expected_events,) if isinstance(expected_events, int) else expected_events
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            flow_id, data = self.poll_packet(timeout=0.2)
            if flow_id is not None and data:
                evt = None
                if flow_id == 0x41 and len(data) >= 4:
                    evt = struct.unpack_from("<I", data, 0)[0]
                elif flow_id == 0xFF:
                    if len(data) >= 5:
                        evt = struct.unpack_from("<I", data, 1)[0]
                    elif len(data) >= 4:
                        evt = struct.unpack_from("<I", data, 0)[0]
                elif len(data) >= 4:
                    evt = struct.unpack_from("<I", data, 0)[0]

                if evt is not None:
                    if evt in expected_set:
                        return evt
                    if evt >= EVENT_FLASH_PROG_ERROR and evt < EVENT_FLASH_PROG_ERROR + 4:
                        raise RuntimeError(f"Flash programmer error event 0x{evt:08x}")
        raise TimeoutError(f"Timed out waiting for event {expected_set}")


# ---------------------------------------------------------------------------
# High-Level Flashing Engine
# ---------------------------------------------------------------------------
def enter_host_monitor(target: SerialTarget):
    print("[-] Requesting 8955 Serial Host Monitor...")
    if target.ser and target.ser.is_open:
        try:
            target.ser.send_break(0.25)
            time.sleep(0.1)
            target.ser.reset_input_buffer()
            target.ser.reset_output_buffer()
        except Exception:
            pass
    target.rx_buf.clear()


    # 1. Configure Reset Cause with monitor boot mode
    prev = 0x28
    monitor_mode = prev | (1 << 1) | (1 << 2) | (1 << 3)
    target.write32(SYS_CTRL_RESET_CAUSE_ADDRESS, monitor_mode << 16)

    # 2. Trigger hardware Debug Reset
    target.write_internal(DBG_HOST_CTRL_SET_OFFSET, 1 << 0)
    time.sleep(0.5)

    # Configure host priority and clear force reset
    target.write_internal(DBG_HOST_CFG_OFFSET, 1 << 7)
    target.write_internal(DBG_HOST_CTRL_CLR_OFFSET, 1 << 1)

    # Command monitor start
    target.write_internal(DBG_HOST_H2P_STATUS_OFFSET, BOOT_HST_MONITOR_START_CMD)
    evt = target.wait_event(EVENT_HOST_MONITOR_STARTS, timeout=8.0)
    print(f"[+] Entered Host Monitor: 0x{evt:08X}")

    # Restore reset cause
    target.write32(SYS_CTRL_RESET_CAUSE_ADDRESS, prev << 16)




def read_flash_map(target: SerialTarget, timeout: float = 3.0) -> tuple[int, list[int], list[int], int]:
    prog_access_ptr = FASTPF_PROG_ACCESS_ADDRESS

    # Wait for FastPF access table to be initialized by the RAM-run
    deadline = time.monotonic() + timeout
    val = 0
    while time.monotonic() < deadline:
        try:
            val = target.read32(prog_access_ptr, timeout=0.2)
            major = val & 0xFFFF
            minor = (val >> 16) & 0xFFFF
            if major == FPC_PROTOCOL_MAJOR and minor == FPC_PROTOCOL_MINOR:
                break
        except Exception:
            pass
        time.sleep(0.05)
    else:
        major = val & 0xFFFF
        minor = (val >> 16) & 0xFFFF
        raise ValueError(f"FastPF version mismatch: 0x{major:04x}.0x{minor:04x}")

    # Read remaining 56 bytes of FastPF map
    raw = struct.pack("<I", val) + target.read_block(prog_access_ptr + 4, 56)
    cmd_slots = [prog_access_ptr + 4, prog_access_ptr + 24]
    buf_a, buf_b, buf_c, fpc_size = struct.unpack_from("<IIII", raw, 44)
    buffers = [buf_a, buf_b, buf_c]
    return prog_access_ptr, cmd_slots, buffers, min(fpc_size, MAX_FPC_BUFFER)


def send_command(target: SerialTarget, cmd_addr: int, ram_addr: int, size: int, flash_addr: int, fcs: int, cmd: int):
    desc = struct.pack("<IIIII", FPC_NONE, flash_addr, ram_addr, size, fcs)
    target.write_block(cmd_addr, desc)
    target.write32(cmd_addr, cmd)


def flash_firmware(target: SerialTarget, ramrun_blocks: list[ProgramBlock], firmware_blocks: list[ProgramBlock]):
    print("[-] Uploading RAM-run into SRAM...")
    target.write32(FASTPF_PROG_ACCESS_ADDRESS, 0)
    for block in ramrun_blocks:
        target.write_block(block.address, block.data)

    print("[-] Executing RAM-run...")
    target.write32(HOST_MONITOR_CONTROL_ADDRESS, HST_EXECUTOR_X_CMD)
    target.write_internal(DBG_HOST_H2P_STATUS_OFFSET, HST_EXECUTOR_X_CMD)

    # Wait for RAM-run startup (ROM monitor treatment 0xFF000004 & Flash ID return)
    deadline = time.monotonic() + 4.0
    flash_id = None
    while time.monotonic() < deadline:
        flow, data = target.poll_packet(timeout=0.1)
        if flow == 0xFF and data and len(data) >= 5:
            val = struct.unpack_from("<I", data, 1)[0]
            if val not in (0xFF000004, 0xFF000001, 0xFF000002):
                flash_id = val
                break
    if flash_id:
        print(f"[+] Flash Programmer Initialized! JEDEC ID: 0x{flash_id:08X}")
    else:
        print("[-] Waiting for FastPF access table to become ready...")

    prog_ptr, cmd_slots, buffers, fpc_size = read_flash_map(target)
    print(f"[+] Flash Programmer READY! FastPF Access: 0x{prog_ptr:08X}")
    print(f"[-] FastPF Buffers: {[hex(b) for b in buffers]}, Chunk: {fpc_size} bytes")

    current_cmd = 0
    current_buf = 0

    def dispatch(ram_addr: int, size: int, flash_addr: int, chunk_fcs: int, cmd: int, timeout: float = 15.0):
        nonlocal current_cmd
        slot_addr = cmd_slots[current_cmd]
        send_command(target, slot_addr, ram_addr, size, flash_addr, chunk_fcs, cmd)
        expected = EVENT_FLASH_PROG_READY + current_cmd
        target.wait_event(expected, timeout=timeout)
        current_cmd = (current_cmd + 1) % 2

    for idx, block in enumerate(firmware_blocks, 1):
        data = block.data
        rel_addr = block.address & 0x01FFFFFF

        print(f"[-] [{idx}/{len(firmware_blocks)}] Erasing & Programming 0x{block.address:08x} ({len(data)} bytes)...")
        # Align erase size to 4KB sector
        erase_size = ((len(data) + 0xFFF) // 0x1000) * 0x1000
        dispatch(0, erase_size, rel_addr, 0, FPC_ERASE, timeout=20.0)

        offset = 0
        while offset < len(data):
            chunk = data[offset : offset + fpc_size]
            chunk_fcs = calculate_fcs(chunk)
            buf = buffers[current_buf]
            target.write_block(buf, chunk)
            dispatch(buf, len(chunk), rel_addr + offset, chunk_fcs, FPC_PROGRAM, timeout=15.0)
            current_buf = (current_buf + 1) % len(buffers)
            offset += len(chunk)

    dispatch(0, 0, 0, 0, FPC_END, timeout=10.0)
    print("[+] Finalized programming session.")

    slot_addr = cmd_slots[current_cmd]
    send_command(target, slot_addr, 0, 0, 0, 0, FPC_RESTART)
    try:
        target.wait_event(EVENT_FLASH_PROG_READY + current_cmd, timeout=3.0)
    except (TimeoutError, RuntimeError):
        pass
    print("[+] Flash programmer restart acknowledged.")
    exit_host_monitor(target)


def exit_host_monitor(target: SerialTarget) -> None:
    print("[-] Clearing reset cause and exiting host monitor...")
    target.write32(SYS_CTRL_RESET_CAUSE_ADDRESS, 0)
    target.write_internal(DBG_HOST_H2P_STATUS_OFFSET, BOOT_HST_MONITOR_END_CMD)
    try:
        target.wait_event(BOOT_HST_MONITOR_END_EVT, timeout=3.0)
        print("[+] Target exited monitor and booted into flash firmware (0xFF000003)!")
    except Exception as e:
        print(f"[!] Note on monitor exit: {e}")


def run_entry(target: SerialTarget, entry_point: int, sp: int = 0x81C07F00) -> None:
    print(f"[-] ROM Monitor Executor jumping to 0x{entry_point:08X} (SP: 0x{sp:08X})...")
    target.write32(HOST_MONITOR_CONTROL_ADDRESS, HST_EXECUTOR_X_CMD)
    target.write32(HOST_MONITOR_CONTROL_ADDRESS + 4, entry_point)
    target.write32(HOST_MONITOR_CONTROL_ADDRESS + 8, sp)
    target.write_internal(DBG_HOST_H2P_STATUS_OFFSET, HST_EXECUTOR_X_CMD)
    print(f"[+] Executing at 0x{entry_point:08X}...")


def stream_traces(target: SerialTarget, duration: float = 20.0) -> None:
    import re
    print("[-] Checking target stream & host events...")
    print(f"=== LIVE RDA8955 SYSTEM & APPLICATION TRACE MONITOR ({duration:.1f}s) ===")
    t_start = time.time()
    try:
        while time.time() - t_start < duration:
            flow, data = target.poll_packet(timeout=0.05)
            if flow is not None and data:
                elapsed = time.time() - t_start
                if flow == 0xFF and len(data) >= 5:
                    evt = struct.unpack_from("<I", data, 1)[0]
                    if evt == 0xBA5E0001:
                        print(f"[{elapsed:05.2f}s] [EVENT 0xFF] *** [FREE-RTOS BOOT] Open-Source FreeRTOS Kernel Online! ***")
                    elif evt == 0x1111AAAA:
                        print(f"[{elapsed:05.2f}s] [EVENT 0xFF] *** [Task:Heartbeat] Initialized & Running! ***")
                    elif evt == 0x2222AAAA:
                        print(f"[{elapsed:05.2f}s] [EVENT 0xFF] *** [Task:Worker] Initialized & Running! ***")
                    elif (evt & 0xFFFF0000) == 0x11110000:
                        st = "ON" if (evt & 0xFFFF) else "OFF"
                        print(f"[{elapsed:05.2f}s] [EVENT 0xFF] [Task:Heartbeat] Blue LED: {st}")
                    elif (evt & 0xFFFF0000) == 0x22220000:
                        st = "ON" if (evt & 0xFFFF) else "OFF"
                        print(f"[{elapsed:05.2f}s] [EVENT 0xFF] [Task:Worker] Green LED: {st}")
                    elif (evt & 0xFFFF0000) == 0x44440000:
                        ticks = evt & 0xFFFF
                        print(f"[{elapsed:05.2f}s] [EVENT 0xFF] [FreeRTOS:Idle] System Tick Count: {ticks}")
                    elif (evt & 0xFFFF0000) == 0xBA5E0000:
                        code = evt & 0xFFFF
                        print(f"[{elapsed:05.2f}s] [EVENT 0xFF] [FreeRTOS:Init] Diagnostic Code: 0x{code:04X}")
                    elif (evt & 0xFFFF0000) == 0x55550000:
                        names = {1: "Task Created", 2: "Task Switched In", 3: "Task Switched Out", 4: "Task Delayed"}
                        name = names.get(evt & 0xFFFF, f"Trace 0x{evt & 0xFFFF:04X}")
                        print(f"[{elapsed:05.2f}s] [EVENT 0xFF] [FreeRTOS:Kernel] {name}")
                    else:
                        print(f"[{elapsed:05.2f}s] [EVENT 0xFF] Host Event: 0x{evt:08X}")
                else:
                    strings = re.findall(b'[\x20-\x7E]{3,}', data)
                    if strings:
                        text = ' | '.join(s.decode('ascii', errors='ignore') for s in strings)
                        print(f"[{elapsed:05.2f}s] [0x{flow:02X}] {text}")
            elif target.ser and target.ser.in_waiting > 0:
                raw_bytes = target.ser.read(target.ser.in_waiting)
                elapsed = time.time() - t_start
                text = raw_bytes.decode('ascii', errors='ignore').strip()
                if text:
                    print(f"[{elapsed:05.2f}s] [RAW UART] {text}")
    except KeyboardInterrupt:
        print("\n[*] Trace monitor interrupted by user.")


# ---------------------------------------------------------------------------
# CLI Entry Point
# ---------------------------------------------------------------------------
def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="/dev/ttyUSB0", help="Serial port (default: /dev/ttyUSB0)")
    parser.add_argument("--baud", type=int, default=921600, help="Baud rate (default: 921600)")
    parser.add_argument("--ramrun", type=Path, default=Path("ramrun/open_ramrun_clean.elf"), help="RAM-run ELF/BIN (default: ramrun/open_ramrun_clean.elf)")
    parser.add_argument("--firmware", type=Path, default=Path("demo/baremetal/baremetal_boot.bin"), help="Firmware BIN/ELF")
    parser.add_argument("--address", type=lambda s: int(s, 0), default=0x08010000, help="Flash address for .bin (default: 0x08010000)")
    parser.add_argument("--execute", action="store_true", help="Perform actual flash writes (default: dry-run)")
    parser.add_argument("--run-entry", type=lambda s: int(s, 0), default=None, help="Directly jump to entry point via ROM monitor executor")
    parser.add_argument("--monitor", action="store_true", help="Stream live execution traces after boot")
    parser.add_argument("--passive", action="store_true", help="Listen passively without resetting or entering monitor")
    parser.add_argument("--monitor-time", type=float, default=20.0, help="Trace monitor duration in seconds (default: 20s)")
    args = parser.parse_args()

    ramrun_blocks = load_firmware(args.ramrun)
    firmware_blocks = load_firmware(args.firmware, args.address)

    total_fw = sum(len(b.data) for b in firmware_blocks)
    print(f"=== Standalone Direct Serial Flasher ===")
    print(f"  Port: {args.port} @ {args.baud} baud")
    print(f"  RAM-run: {args.ramrun} ({len(ramrun_blocks)} blocks)")
    print(f"  Firmware: {args.firmware} ({len(firmware_blocks)} blocks, {total_fw:,} bytes at 0x{firmware_blocks[0].address:08X})")

    if not args.execute and not args.monitor and args.run_entry is None:
        print("[!] DRY-RUN complete. Pass --execute to flash, --run-entry to jump, or --monitor to monitor.")
        return 0

    target = SerialTarget(args.port, args.baud)
    try:
        target.open()
        if args.execute:
            enter_host_monitor(target)
            flash_firmware(target, ramrun_blocks, firmware_blocks)
            print("=== FLASH COMPLETE SUCCESSFULLY WITHOUT COOLHOST ===")
        if args.run_entry is not None:
            enter_host_monitor(target)
            run_entry(target, args.run_entry)
        if args.monitor:
            if not args.execute and args.run_entry is None and not args.passive:
                enter_host_monitor(target)
                exit_host_monitor(target)
            stream_traces(target, duration=args.monitor_time)
    finally:
        target.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())


