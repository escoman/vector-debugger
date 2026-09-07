---
platform: Vector-06C
topic: architecture
status: verified
source: emulator (board.cpp, vio.h, memory.cpp, filler.cpp)
---

# Vector-06C Architecture

## Overview

Vector-06C (Вектор-06Ц) is a Soviet home computer based on the KR580VM80A (Intel 8080) CPU.

## Key Components

| Component | Chip | Role |
|-----------|------|------|
| CPU | KR580VM80A (i8080) | Central processor, 3 MHz |
| PIA1 | KR580VV55A (i8255) | System peripheral interface |
| PPI2 | KR580VV55A (i8255) | Auxiliary peripheral interface |
| Timer | KR580WI53 (i8253) | Programmable interval timer |
| Memory | RAM 64 KB + expansion | Main memory with paging |
| VRAM | 16 KB (mapped at 0xC000) | Video memory |
| AY-3-8912 | Programmable sound generator | 3-channel sound |
| FD1793 | Floppy disk controller | Disk drive interface |

## Bus Architecture

CPU communicates with peripherals through I/O port addressing (IN/OUT instructions).
Memory is accessed through a 16-bit address bus with optional paging (Bigram mode).

## Reset Behavior

On power-on reset (BLKVVOD mode):
- PC = 0x0000
- SP is preserved by boot ROM (typically 0xC300)
- Boot ROM is mapped at 0x0000–0x0FFF (2 KB)
- Boot ROM detaches after first jump to user code

On BLKSBR reset:
- Similar to power-on but preserves more state

## Interrupt Architecture

- Single interrupt line (INTR)
- Interrupt enable flag (INTE/IFF)
- RST 0x38 (restart vector at 0x0038) is the standard interrupt handler entry
- Timer generates frame interrupts (50 Hz)

## Timing

- CPU clock: 3 MHz
- Frame rate: 50 Hz
- Raster: 768 columns × 312 lines (in pixel clock units)
- Timer clock: CPU clock / 8 = 375 kHz

## Address Space

```
0x0000–0x00FF  Interrupt vector table
0x0100–0xBFFF  User RAM (or ROM if loaded at 0x0100)
0xC000–0xCFFF  VRAM Plane A (text/graphics)
0xD000–0xDFFF  VRAM Plane B (graphics, second plane)
0xE000–0xFFFF  System RAM / VRAM extension
```

Note: Actual VRAM layout depends on video mode (256 or 512 column).

## Data Flow

```
CPU ←→ Memory (64 KB addressable, with Bigram paging up to 320 KB physical)
CPU ←→ I/O ports (256 ports, IN/OUT instructions)
PIA1 ←→ Keyboard, cassette, system control
PPI2 ←→ Covox, auxiliary I/O
Timer ←→ Frame timing, sound
AY-3-8912 ←→ Sound synthesis
FD1793 ←→ Floppy disk
```
