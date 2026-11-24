#pragma once

#include "glass.h"

typedef struct {
    int rescue;
    int charging;
    int have_imu;
    int have_ear;
    glass_imu imu;
    glass_touch touch;
    glass_ear ear;
} board_sample;

void board_init(void);
void board_latch_touch(void);
void board_read(board_sample *out);
void board_noise(int kind);
void board_tick(int on);
void board_chime(void);
