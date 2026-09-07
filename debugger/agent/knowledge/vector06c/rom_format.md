---
platform: Vector-06C
topic: rom_format
status: verified
source: emulator (emulator.cpp, board.cpp)
---

# Vector-06C ROM Formats

## File Extensions

| Extension | Load Address | Description |
|-----------|-------------|-------------|
| .rom | 0x0100 | Standard ROM — loaded after interrupt vector table |
| .r0m | 0x0000 | Raw memory image — loaded at address 0 |
| .bin | varies | Generic binary data |

## ROM Loading Conventions

### .rom files

The standard Vector-06C ROM format. Loaded at address 0x0100 because:
- Addresses 0x0000–0x00FF are reserved for the interrupt vector table
- The vector table contains 64 entries × 4 bytes = 256 bytes
- ROM code starts at 0x0100

### .r0m files

Raw memory images loaded at address 0x0000. These contain a complete memory snapshot including the vector table.

### MAP files

Z88DK-compatible MAP files provide symbol information:
- Function names and addresses
- Label definitions
- Used by debugger for symbol resolution

A MAP file is typically paired with a ROM:
```
program.rom
program.map
```

## Boot Sequence

1. On reset, 2 KB boot ROM is mapped at 0x0000–0x07FF
2. Boot ROM initializes hardware and loads the main program
3. Boot ROM detaches, revealing RAM at 0x0000
4. Standard initial SP after boot: 0xC300
5. Program entry typically at 0x0100

## Interrupt Vector Table

```
Address 0x0000–0x00FF:
  Entry 0:  RST 0 handler  (address 0x0000)
  Entry 1:  RST 1 handler  (address 0x0008)
  ...
  Entry 7:  RST 7 handler  (address 0x0038) — used for INTR interrupt
  ...
  Entry 63: last vector    (address 0x00FC)
```

Each entry is a 4-byte jump instruction (JP addr).

## ROM Size

Typical ROM sizes:
- 4 KB – small utilities
- 8–16 KB — standard programs
- 32–48 KB — large applications (approaching available user space)

Maximum user program space: 0x0100–0xBFFF (~48 KB), limited by VRAM at 0xC000.
