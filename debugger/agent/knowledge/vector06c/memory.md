---
platform: Vector-06C
topic: memory
status: verified
source: emulator (memory.h, memory.cpp)
---

# Vector-06C Memory System

## Physical Memory

Total physical memory: 64 KB main RAM + 256 KB expansion = 320 KB.

```c
#define TOTAL_MEMORY (64 * 1024 + 256 * 1024)  // 327680 bytes
```

## Address Translation (Bigram Paging)

The Vector-06C supports memory paging through the "Bigram" (Кваз) controller at I/O port 0x10.

### Control Register (port 0x10)

| Bits | Function |
|------|----------|
| 0–1 | Map page select: page_map = ((bits[1:0] + 1) << 16) |
| 2–3 | Stack page select: page_stack = ((bits[3:2] + 1) << 16) |
| 4 | Stack paging mode enable |
| 5 | Map paging mode enable |

### Address Mapping

Without paging (default):
- Physical address = logical address (1:1 mapping for 0x0000–0xFFFF)

With stack paging (mode_stack = 1, stack access):
- Physical = logical + page_stack
- Applies only to stack operations (PUSH, POP, CALL, RET, XTHL, etc.)

With map paging (mode_map = 1, address 0xA000–0xDFFF):
- Physical = logical + page_map
- Applies only to accesses in range 0xA000–0xDFFF

### Physical Address Calculation

```
bigram_addr = bigram_select(logical_addr, is_stack_access)
physical_addr = tobank(bigram_addr)
```

The `tobank` function interleaves address bits for DRAM banking:
```
physical = (addr & 0x78000) | ((addr << 2) & 0x7FFC) | ((addr >> 13) & 3)
```

## Boot ROM

On reset, a 2 KB boot ROM is mapped at 0x0000–0x07FF:
- Boot ROM overlays the first bytes of RAM
- After the boot sequence completes, boot ROM detaches
- Standard SP after boot: 0xC300

## ROM Loading Conventions

| Extension | Load Address | Description |
|-----------|-------------|-------------|
| .rom | 0x0100 | After interrupt vector table (0x0000–0x00FF) |
| .r0m | 0x0000 | Raw memory image |
| .bin | varies | Binary data |

## Memory Map (Typical)

```
0x0000–0x00FF  Interrupt vector table (256 bytes, 64 entries × 4 bytes)
0x0100–0xBFFF  User program space (up to ~48 KB)
0xC000–0xCFFF  VRAM Plane A (4 KB, 256×8 mode) or (8 KB, 512×4 mode)
0xD000–0xDFFF  VRAM Plane B (4 KB, second graphics plane)
0xE000–0xFFFF  System RAM (8 KB)
```

Note: VRAM occupies the 0xC000–0xFFFF range but the exact split between VRAM and RAM depends on the video mode and Bigram paging configuration.
