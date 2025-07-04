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
int Emulator_ExecuteFrame(float * samples);
void Emulator_KeyDown(int scancode);
void Emulator_KeyUp(int scancode);
void Emulator_LoadAsset(const uint8_t *data, size_t data_sz, int kind, int org);
void Emulator_Reset(int blkvvod);
void Emulator_SetJoysticks(int joy0e, int joy0f);
size_t Emulator_ExportState(uint8_t *data, size_t data_sz);
bool Emulator_RestoreState(const void *data, size_t data_sz);
size_t Emulator_GetMemSize(void);
void * Emulator_GetMemory(void);
uint32_t * Emulator_GetPixels();  // frame buffer

#ifdef __cplusplus
}
#endif
