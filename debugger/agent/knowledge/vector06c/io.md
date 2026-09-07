---
platform: Vector-06C
topic: io
status: verified
source: emulator (vio.h)
---

# Vector-06C I/O Port Map

## Overview

The Vector-06C uses IN/OUT instructions to access I/O ports (8-bit port space, 0x00–0xFF).
Ports are grouped by function: PIA1, PPI2, Timer, Palette, Kvas (paging), AY, FDC.

## PIA1 — Parallel Interface Adapter (ports 0x00–0x03)

| Port | Direction | Function |
|------|-----------|----------|
| 0x00 | OUT | Control Word / Port C bit-set-reset |
| 0x01 | OUT | Port C (control bits) |
| 0x02 | IN/OUT | Port B (video mode, border, keyboard read) |
| 0x03 | IN/OUT | Port A (vertical scroll value) |

### Control Word (port 0x00)

Written byte configures PIA1 direction and mode:

| Bit | Function |
|-----|----------|
| 7 | Mode set (1 = mode set active, 0 = port C bit operation) |
| 6 | Port A direction (1 = input, 0 = output) |
| 5 | Port C upper direction (1 = input, 0 = output) |
| 4 | Port B mode (1 = input, 0 = output) |
| 3 | Port B direction (1 = input, 0 = output) |
| 2 | Port C lower direction (1 = input, 0 = output) |
| 1 | Port A mode (1 = input, 0 = output) |
| 0 | Port C bit operation: 1 = set, 0 = reset (when bit 7 = 0) |

Default CW after reset: 0x08.

### Port C (0x01)

| Bit | Function |
|-----|----------|
| 0 | Tape output |
| 3 | Rus/Lat indicator (0 = Rus, 1 = Lat) |
| 4–7 | Keyboard shift status / tape input (when configured as input) |

Bit 4–7 (input mode):
- Bit 4: tape input (from tape player sample)
- Bit 5: Shift (SS) status (0 = pressed)
- Bit 6: Ctrl (US) status (0 = pressed)
- Bit 7: Rus (0 = pressed)

### Port B (0x02)

| Bit | Function |
|-----|----------|
| 0–3 | Border color index (0–15) |
| 4 | Video mode: 0 = 256-column, 1 = 512-column |
| 5–7 | Keyboard scan return (when CW bit 1 = 1) |

### Port A (0x03)

Vertical scroll value (0–255). Stored as PA register.

## PPI2 — Second Parallel Interface (ports 0x04–0x07)

| Port | Function |
|------|----------|
| 0x04 | Control Word 2 |
| 0x05 | Port C2 |
| 0x06 | Port B2 |
| 0x07 | Port A2 (Covox output) |

PPI2 is used for secondary I/O. Port A2 (0x07) serves as a Covox (DAC) output.

## Timer — KR580VI53 (ports 0x08–0x0B)

| Port | Function |
|------|----------|
| 0x08 | Counter 0 data |
| 0x09 | Counter 1 data |
| 0x0A | Counter 2 data |
| 0x0B | Control word |

Used for system timing, sound generation, and baud rate.

## Palette (ports 0x0C–0x0F)

All four ports (0x0C, 0x0D, 0x0E, 0x0F) write the same palette register.
The palette index is auto-incremented by the video hardware after each scanline.

Palette byte format: `BBGGGRRR`
- Bits 7–6: Blue (0–3)
- Bits 5–3: Green (0–7)
- Bits 2–0: Red (0–7)

## Kvas — Memory Paging Controller (port 0x10)

| Bits | Function |
|------|----------|
| 0–1 | Map page select |
| 2–3 | Stack page select |
| 4 | Stack paging mode enable |
| 5 | Map paging mode enable |

See `memory.md` for full details on address translation.

## AY-3-8912 — Sound Generator (ports 0x14–0x15)

| Port | Function |
|------|----------|
| 0x14 | Data read/write (register addressed by current index) |
| 0x15 | Address (register index, 0x00–0x0F) |

Write sequence: OUT 0x15 with register number, then OUT 0x14 with value.
Read sequence: OUT 0x15 with register number, then IN 0x14.

See `sound.md` for AY register details.

## FD1793 — Floppy Disk Controller (ports 0x18–0x1C)

| Port | Read | Write |
|------|------|-------|
| 0x18 | Data register | Data register |
| 0x19 | Sector register | Sector register |
| 0x1A | Track register | Track register |
| 0x1B | Status register | Command register |
| 0x1C | — (not implemented) | Control register |

## Unmapped Ports

Reading unmapped ports returns 0xFF.
Writing to unmapped ports is silently ignored.

## Port Summary Table

| Range | Device |
|-------|--------|
| 0x00–0x03 | PIA1 (keyboard, video, scroll, tape) |
| 0x04–0x07 | PPI2 (Covox, secondary I/O) |
| 0x08–0x0B | Timer (KR580VI53) |
| 0x0C–0x0F | Palette |
| 0x10 | Kvas (memory paging) |
| 0x14–0x15 | AY-3-8912 |
| 0x18–0x1C | FD1793 (FDC) |
| Other | Unmapped (read = 0xFF, write = noop) |
