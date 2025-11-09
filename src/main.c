#include <gb/gb.h>
#include <gbdk/console.h>
#include <rand.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

// Include assets
#include "assets/logo.h"

// -----------------------------------------------------------------------------
// Variables / Constants
// -----------------------------------------------------------------------------

#define VERSION "v0.9.0"

#define AUD3WAVE_LEN 16
#define SAMPLES 32

bool play = false;

uint8_t _wave[SAMPLES];

#define PERIOD_VALUE_MIN 0
#define PERIOD_VALUE_MAX 0x7FF
uint16_t period_value = 1792;

// -----------------------------------------------------------------------------
// Inputs
// -----------------------------------------------------------------------------

uint8_t previous_keys = 0, keys = 0;

void update_keys(void) {
  previous_keys = keys;
  keys = joypad();
}

uint8_t key_pressed(uint8_t aKey) { return keys & aKey; }
uint8_t key_ticked(uint8_t aKey) {
  return (keys & aKey) && !(previous_keys & aKey);
}
uint8_t key_released(uint8_t aKey) {
  return !(keys & aKey) && (previous_keys & aKey);
}

// -----------------------------------------------------------------------------
// Visualization
// -----------------------------------------------------------------------------

// TODO: Visualization / reload_tile
#define VISUALIZATION_WIDTH 17

const uint8_t *scanline_offsets = _wave;
void scanline_isr(void) {
  SCX_REG = scanline_offsets[(uint8_t)(LY_REG & 0x07u)];
}

void reload_tiles(void) {
  for (uint8_t i = 0; i < VISUALIZATION_WIDTH + 1; i++) {
    uint8_t _tile[16] = {rand(), rand(), rand(), rand(), rand(), rand(),
                         rand(), rand(), rand(), rand(), rand(), rand(),
                         rand(), rand(), rand(), rand()};
    set_bkg_data(0x66 + i, 1, _tile);
  }
}

void draw(void) {
  gotoxy(13, 1);
  printf("Shifty");
  gotoxy(0, 17);
  printf(VERSION);

  reload_tiles();
  for (uint8_t i = 0; i < VISUALIZATION_WIDTH + 1; i++)
    for (uint8_t j = 0; j < 14; j++)
      set_bkg_tile_xy(1 + i, 2 + j, 0x66 + i);
}

// -----------------------------------------------------------------------------
// main
// -----------------------------------------------------------------------------

// TODO: Better wave initialization.
void reinit_wave(void) {
  for (uint8_t i = 0; i < SAMPLES; i++) {
    _wave[i] = 0;
  }
  _wave[rand() & 31] = rand() & 0xF;
  _wave[rand() & 31] = rand() & 0xF;
  _wave[rand() & 31] = rand() & 0xF;
}

// Bit numbering: LSb (1) / MSb (0)
// https://en.wikipedia.org/wiki/Bit_numbering
#define BIT_NUMBERING_LSB 1
uint8_t last_sample, lfsr_bit, shifted_bit, shifted_bit_new;

void shiftwave(void) {
  last_sample = _wave[SAMPLES - 1];
#if BIT_NUMBERING_LSB
  lfsr_bit = ((last_sample >> 3) ^ (last_sample >> 2) & 1);
#else
  lfsr_bit = ((last_sample >> 1) ^ last_sample) & 1;
#endif

  shifted_bit = 0;
  // shifted_bit = last_sample >> 3;
  shifted_bit_new = 0;
  for (uint8_t i = 0; i < SAMPLES; i++) {
#if BIT_NUMBERING_LSB
    shifted_bit_new = (_wave[i] >> 3) & 1;
    _wave[i] = ((_wave[i] << 1) | shifted_bit) & 0xF;
#else
    shifted_bit_new = _wave[i] & 1;
    _wave[i] = (_wave[i] >> 1 | (shifted_bit << 3)) & 0xF;
#endif
    shifted_bit = shifted_bit_new;
  }
  _wave[0] |= lfsr_bit;
}

void play_isr(void) {
  if (play) {
    shiftwave();

    NR30_REG = 0;

    for (uint8_t i = 0; i < AUD3WAVE_LEN; i++)
      AUD3WAVE[i] = (_wave[i * 2] << 4) | _wave[i * 2 + 1];

    NR30_REG = 0x80;
    NR31_REG = 0xFE;
    NR32_REG = 0x20;

    NR33_REG = period_value & 0xFF;
    NR34_REG = 0xC0 | (period_value >> 8);
  }
}

void check_inputs(void) {
  update_keys();
  if (key_ticked(J_START)) {
    play = !play;
    if (play) {
      reinit_wave();
    } else {
      for (uint8_t i = 0; i < SAMPLES; i++)
        _wave[i] = 0;
      reload_tiles();
    }
  }
  if (key_ticked(J_SELECT)) {
    if (play) {
      play = false;
      reinit_wave();
      play = true;
    }
  }

  if (key_pressed(J_A)) {
    if (key_pressed(J_UP))
      if (period_value + 10 <= PERIOD_VALUE_MAX)
        period_value += 10;
    if (key_pressed(J_DOWN))
      if (period_value >= PERIOD_VALUE_MIN + 10)
        period_value -= 10;
  } else {
    if (key_pressed(J_UP))
      if (period_value + 1 <= PERIOD_VALUE_MAX)
        period_value++;
    if (key_pressed(J_DOWN))
      if (period_value >= PERIOD_VALUE_MIN + 1)
        period_value--;
  }
}

void show_logo(void) {
  gotoxy(7, 4);
  printf("Shifty");

  // Logo
  set_bkg_tiles(5, 5, logo_WIDTH / 8, logo_HEIGHT / 8, logo_map);

  gotoxy(5, 13);
  printf("by  vec2pt");

  vsync();
  delay(2200);
}

uint16_t seed;
void setup(void) {
  // Random seed
  seed = DIV_REG;
  seed |= (uint16_t)DIV_REG << 8;
  initrand(seed);

  // Sound
  NR52_REG = 0x80;
  NR51_REG = 0x44;
  NR50_REG = 0x77;

  set_bkg_data(0x66, logo_TILE_COUNT, logo_tiles);
  show_logo();
}

void main(void) {
  setup();
  draw();

  __critical {
    TMA_REG = 0xC0, TAC_REG = 0x07;
    add_TIM(play_isr);

    STAT_REG = STATF_MODE00;
    add_LCD(scanline_isr);
    add_LCD(nowait_int_handler);
    set_interrupts(VBL_IFLAG | TIM_IFLAG | IE_REG | LCD_IFLAG);
  }

  while (1) {
    check_inputs();
    vsync();
  }
}
