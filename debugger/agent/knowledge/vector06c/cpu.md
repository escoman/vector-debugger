---
platform: Vector-06C
topic: cpu
status: verified
source: emulator (i8080.h, i8080.cpp)
---

# KR580VM80A (Intel 8080) CPU

## Overview

The Vector-06C uses the KR580VM80A, a Soviet clone of the Intel 8080 CPU, running at 3 MHz.

## Registers

| Register | Width | Description |
|----------|-------|-------------|
| A | 8-bit | Accumulator |
| B, C | 8-bit | General purpose (BC pair = 16-bit) |
| D, E | 8-bit | General purpose (DE pair = 16-bit) |
| H, L | 8-bit | General purpose (HL pair = 16-bit, often address pointer) |
| SP | 16-bit | Stack Pointer |
| PC | 16-bit | Program Counter |
| F | 8-bit | Flags register |

## Flag Register (F)

| Bit | Name | Description |
|-----|------|-------------|
| 7 | Sign (S) | Set if result is negative (bit 7 = 1) |
| 6 | Zero (Z) | Set if result is zero |
| 4 | Half-carry (AC/AUX) | Set if carry from bit 3 to bit 4 |
| 2 | Parity (P) | Set if result has even parity |
| 1 | Carry (CY) | Set if carry/borrow from bit 7 |
| 0,3,5 | — | Always 0 (unused) |

Flag order in F byte: SZ0A0P1C (bits 7–0, where 0 = always zero).

## Interrupt

- INTR pin: maskable interrupt
- INTE (Interrupt Enable Flag): controlled by EI/DI instructions
- On interrupt, CPU executes RST 7 (restart at 0x0038)
- IFF (inner flip-flop) tracks interrupt state; distinct from INTE pin

## Stack Operations

Instructions affecting SP:
- PUSH rp (BC, DE, HL, PSW) — SP -= 2, write word
- POP rp — read word, SP += 2
- CALL addr — SP -= 2, push PC, PC = addr
- RET — PC = pop(), SP += 2
- XTHL — swap HL with word at (SP)
- SPHL — SP = HL
- RST n — SP -= 2, push PC, PC = n*8
- DAD rp — does NOT affect SP

## Key Instruction Groups

| Group | Examples | Notes |
|-------|----------|-------|
| Data transfer | MOV, MVI, LDA, STA, LHLD, SHLD, LDAX, STAX | |
| Arithmetic | ADD, ADC, SUB, SBB, INR, DCR, DAD, INX, DCX | |
| Logic | ANA, XRA, ORA, CMP, RLC, RRC, RAL, RAR | |
| Branch | JMP, Jcc, CALL, Ccc, RET, Rcc, RST, PCHL | |
| Stack | PUSH, POP, XTHL, SPHL | |
| I/O | IN port, OUT port | Port is 8-bit (0–255) |
| Control | EI, DI, HLT, NOP | |

## Cycle Timing

Each instruction takes 4–18 T-states (CPU clock cycles at 3 MHz).
Common instructions: MOV (5 T), MVI (7 T), JMP (10 T), CALL (17 T), RET (10 T).
