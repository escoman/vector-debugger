---
platform: Vector-06C
topic: sound
status: verified
source: emulator (ay.h, sound.h, 8253.h)
---

# Vector-06C Sound System

## Overview

The Vector-06C sound system consists of:
1. AY-3-8912 programmable sound generator (3 channels + noise + envelope)
2. KR580VI53 timer (used for tone generation and system timing)
3. Covox DAC (simple 8-bit DAC on PPI2 Port A, port 0x07)
4. Tape I/O sound (input/output through PIA1 Port C)

## AY-3-8912

### Access

- Port 0x15: register address (0x00–0x0F)
- Port 0x14: register data

### Registers

| Reg | Bits | Function |
|-----|------|----------|
| 0x00 | 0xFF | Channel A tone period (low) |
| 0x01 | 0x0F | Channel A tone period (high) |
| 0x02 | 0xFF | Channel B tone period (low) |
| 0x03 | 0x0F | Channel B tone period (high) |
| 0x04 | 0xFF | Channel C tone period (low) |
| 0x05 | 0x0F | Channel C tone period (high) |
| 0x06 | 0x1F | Noise period |
| 0x07 | 0xFF | Mixer control: `xxNCNBNA TCTBTA` |
| 0x08 | 0x1F | Channel A amplitude (0x00–0x0F) or envelope mode (bit 4) |
| 0x09 | 0x1F | Channel B amplitude / envelope mode |
| 0x0A | 0x1F | Channel C amplitude / envelope mode |
| 0x0B | 0xFF | Envelope period (low) |
| 0x0C | 0xFF | Envelope period (high) |
| 0x0D | 0x0F | Envelope shape: CONT\|ATT\|ALT\|HOLD |

### Mixer Control (register 0x07)

| Bit | Function |
|-----|----------|
| 0 | Channel A tone enable (0 = enabled) |
| 1 | Channel B tone enable |
| 2 | Channel C tone enable |
| 3 | Channel A noise enable (0 = enabled) |
| 4 | Channel B noise enable |
| 5 | Channel C noise enable |
| 6–7 | Unused |

### Amplitude (registers 0x08–0x0A)

- Bits 0–3: fixed amplitude level (0–15)
- Bit 4: 1 = use envelope, 0 = use fixed amplitude

### Envelope Shapes (register 0x0D)

| CONT | ATT | ALT | HOLD | Behavior |
|------|-----|-----|------|----------|
| 0 | 0 | x | x | Sawtooth down, then hold at 0 |
| 0 | 1 | x | x | Sawtooth down, then hold at 0 |
| 1 | 0 | 0 | 0 | Single sawtooth up |
| 1 | 0 | 0 | 1 | Sawtooth up, hold at 15 |
| 1 | 0 | 1 | 0 | Sawtooth up, then down, repeat |
| 1 | 0 | 1 | 1 | Sawtooth up, then down, hold at 0 |
| 1 | 1 | 0 | 0 | Sawtooth down, then hold at 0 |
| 1 | 1 | 0 | 1 | Sawtooth down, hold at 0 |
| 1 | 1 | 1 | 0 | Sawtooth down, then up, repeat |
| 1 | 1 | 1 | 1 | Sawtooth down, then up, hold at 15 |

### 16-step Amplitude Table

```
Step:  0     1     2     3     4     5     6     7
Vol:  0.00  0.014 0.021 0.029 0.042 0.062 0.085 0.137

Step:  8     9    10    11    12    13    14    15
Vol:  0.169 0.265 0.353 0.450 0.570 0.687 0.848 1.000
```

### Noise Generator

17-bit LFSR with polynomial 0x24000.
Noise period register (0x06) sets clock divider.

## Covox DAC

Simple resistor-ladder DAC connected to PPI2 Port A (port 0x07).
8-bit unsigned samples, output value directly written via OUT 0x07.

## Sound Timing

The AY emulator runs at 1.5 MHz clock divided by the CPU instruction timing.
The `AYWrapper::step2()` method accumulates CPU T-states and generates samples at the appropriate rate.

Output is mixed: 3 AY channels summed and scaled by 0.333, plus Covox and tape signals.
