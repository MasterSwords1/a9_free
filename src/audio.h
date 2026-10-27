#ifndef AUDIO_H
#define AUDIO_H

#include <stdint.h>
#include <stdbool.h>

/*
 * RDA8955 Audio Subsystem Driver (AIF + ABB Codec)
 *
 * AIF: Audio InterFace (digital, 0xA1927000)
 * ABB: Analog BaseBand Codec (ISPI chip select 1)
 */

/* ABB register access over internal SPI (ISPI CS 1) */
uint16_t abb_read(uint16_t reg);
void     abb_write(uint16_t reg, uint16_t val);

/* High-level audio controls */
void     audio_init(void);
uint16_t audio_get_chip_id(void);
void     audio_speaker_enable(bool en);
bool     audio_speaker_is_enabled(void);
void     audio_set_volume(uint8_t vol); /* 0 (mute) .. 15 (max) */
uint8_t  audio_get_volume(void);

/* Tone synthesis via AIF hardware tone generator */
void     audio_tone_play(uint8_t tone_id, uint8_t gain);
void     audio_tone_stop(void);
void     audio_beep(uint32_t duration_ms);

#endif /* AUDIO_H */
