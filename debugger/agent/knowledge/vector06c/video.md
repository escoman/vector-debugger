---
platform: Vector-06C
topic: video
status: verified
source: emulator (filler.cpp, filler.h, vram_mapping.h, globaldefs.h)
---

# Vector-06C Video System

## Overview

The Vector-06C video system generates a 512×256 or 256×256 pixel display with a 16-color palette.

## Video Modes

| Mode | Resolution | Pixels/Byte | VRAM Usage |
|------|-----------|-------------|------------|
| 256-column | 256×256 | 8 | 256×256/8 = 8 KB per plane |
| 512-column | 512×256 | 4 | 512×256/4 = 32 KB per plane |

Mode selection: PIA1 port B bit 4 (port 0x02).
- 0 = 256-column mode
- 1 = 512-column mode

## Screen Layout

```
Total raster: 768 × 312 (pixel clock units)
Visible area: 512 × 256 pixels
Border: 32 pixels left/right, 16 pixels top/bottom
Frame buffer: 576 × 288 pixels (visible + border)
```

## VRAM Addressing

VRAM base address: 0xC000.

### 256-column mode

```
VRAM_addr = 0xC000 + vramCol × 256 + vramRow
vramCol   = visibleX / 8     (8 pixels per byte)
vramRow   = (scroll + visibleLine) & 0xFF
```

Each byte represents 8 horizontal pixels. Bit 7 = leftmost pixel.

### 512-column mode

```
vramCol   = visibleX / 4     (4 pixels per byte)
```

Each byte represents 4 horizontal pixels. Bits 7,5,3,1 = leftmost to rightmost.

## Scrolling

Vertical scroll value: PIA1 port A (port 0x03), range 0–255.
```
vramRow = (scrollValue + visibleLine) & 0xFF
```

## Palette

16 colors, each defined by a byte written to ports 0x0C–0x0F:

```
Palette byte: BBGGGRRR
  bits 7–6: Blue (0–3)
  bits 5–3: Green (0–7)
  bits 2–0: Red (0–7)
```

Palette index is selected by the VRAM data bits (4 bits per pixel in 256 mode, 2 bits per pixel in 512 mode).

### Boot Palette (Yellow/Blue "Yeblette")

```
Index with bit 1 set: Blue  (R=0, G=0, B=2)
Index with bit 1 clear: Yellow (R=5, G=5, B=0)
```

## Border Color

Border color index: PIA1 port B bits 0–3 (port 0x02, masked with 0x0F).
Uses the same 16-color palette.

## Planes

The Vector-06C supports two VRAM planes:
- Plane A: base at 0xC000
- Plane B: base at 0xD000

Planes can be combined for overlay effects.

## Timing

- Frame rate: 50 Hz
- Raster: 312 lines total
- Visible lines: 256 (lines 16–271 in framebuffer coordinates)
