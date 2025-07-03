#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    LOADKIND_ROM = 0,
    LOADKIND_COM = 1,
    LOADKIND_FDD = 2,
    LOADKIND_EDD = 3,
    LOADKIND_WAV = 4
};

int Emulator_Init(void);
int Emulator_ExecuteFrame(uint8_t * pixels, float * samples);
void Emulator_KeyDown(int scancode);
void Emulator_KeyUp(int scancode);
void Emulator_LoadAsset(const uint8_t *data, size_t data_sz, int kind, int org);
void Emulator_Reset(int blkvvod);

#ifdef __cplusplus
}
#endif
