---
platform: Vector-06C
topic: keyboard
status: verified
source: emulator (keyboard.h, vio.h)
---

# Vector-06C Keyboard

## Overview

The Vector-06C keyboard is an 8×8 matrix scanned through PIA1 ports.
The matrix encodes approximately 64 keys plus 3 shift keys.

## Matrix Layout

```
  │ 7   6   5   4   3   2   1   0
──┼───────────────────────────────
7 │SPC  ^   ]   \   [   Z   Y   X
6 │ W   V   U   T   S   R   Q   P
5 │ O   N   M   L   K   J   I   H
4 │ G   F   E   D   C   B   A   @
3 │ /   .   =   ,   ;   :   9   8
2 │ 7   6   5   4   3   2   1   0
1 │F5  F4  F3  F2  F1  AP2 CTP ^\ -
0 │DN  RT  UP  LT  ЗАБ ВК  ПС  TAB
```

Column names: Russian labels (ПС = Enter, ЗАБ = Backspace, ВК = input, AP2 = Esc, CTP = F7/Screen).

## Scanning Mechanism

1. CPU writes row select mask to PIA1 Port A (port 0x00, via CW configuration).
2. CPU reads Port B (port 0x02) to get column data.
3. Each bit in the returned byte corresponds to a column (0 = pressed, active low).

The `keyboard.read(rowbit)` function ORs together all selected rows and returns the inverted result:

```c
int read(int rowbit) {
    int result = 0;
    for (int i = 0; i < 8; ++i) {
        if ((rowbit & 1) != 0)
            result |= matrix[i];
        rowbit >>= 1;
    }
    return (~result) & 0xFF;
}
```

## Shift Keys

Shift keys are not part of the matrix. They are read from Port C (port 0x01):

| Key | Signal | Port C bit | Active |
|-----|--------|-----------|--------|
| Shift (SS) | Left/Right Shift | Bit 5 | 0 = pressed |
| Ctrl (US) | Control | Bit 6 | 0 = pressed |
| Rus | Rus/Lat toggle | Bit 7 | 0 = pressed |

These bits are read when PIA1 CW bit 3 = 1 (Port C upper configured as input).

## Key Encoding

Each key is encoded as a (column, bit) pair:
- Column: 0–7 (selects which matrix row)
- Bit: one of {0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80}

Example: Space = column 7, bit 0x80 → encoded as 0x780.

## Special Keys

| Key | Function |
|-----|----------|
| F11 | Hard reset |
| F12 | Soft reset |
| Pause/Break | Terminate (set terminate flag) |

## Tape I/O

- Tape output: PIA1 Port C bit 0
- Tape input: PIA1 Port C bit 4 (read when CW bit 3 = 1)
