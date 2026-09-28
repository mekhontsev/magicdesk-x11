#pragma once
#include "output_command.h"

// X-thread-owned logical devices. Slots are bounded and never allocated per event.
void lorieDirectInput(uint32_t output, const LorieOutputCommand*, int rootX, int rootY);
void lorieDirectInputRelease(uint32_t output);
void lorieDirectInputReset(void);
int lorieDirectInputPressed(uint32_t output);
