/*
 * RDA8955 Audio Subsystem Driver (AIF + ABB Codec)
 *
 * Implements digital AIF control (0xA1927000) and analog ABB codec
 * control via ISPI (SPI3, chip select 1) based on 8955_hard.xml.
 */

#include "audio.h"
#include "FreeRTOS.h"
#include "task.h"

/*
 * ISPI Registers (SPI3, 0xA1A13000)
 */
#define ISPI_BASE        0xA1A13000
#define REG_ISPI_STATUS  (*(volatile uint32_t *)(ISPI_BASE + 0x04))
#define REG_ISPI_FIFO    (*(volatile uint32_t *)(ISPI_BASE + 0x08))

#define ISPI_TX_SPACE(s) (((s) >> 16) & 0x1F)
#define ISPI_RX_LEVEL(s) (((s) >> 24) & 0x1F)
#define ISPI_BUSY        0x01
#define ISPI_READ_CS1    0xA2000000u   /* Read flag + CS 1 */
#define ISPI_WRITE_CS1   0x20000000u   /* CS 1 */

static void ispi_wait_idle(void)
{
    uint32_t s;
    do {
        s = REG_ISPI_STATUS;
    } while ((s & ISPI_BUSY) || ISPI_TX_SPACE(s) != 16);
}

uint16_t abb_read(uint16_t reg)
{
    taskENTER_CRITICAL();
    while (ISPI_TX_SPACE(REG_ISPI_STATUS) == 0);
    REG_ISPI_FIFO = ISPI_READ_CS1 | (((uint32_t)(reg) & 0x1FF) << 16);
    ispi_wait_idle();
    while (ISPI_RX_LEVEL(REG_ISPI_STATUS) == 0);
    uint16_t v = (uint16_t)(REG_ISPI_FIFO & 0xFFFF);
    taskEXIT_CRITICAL();
    return v;
}

void abb_write(uint16_t reg, uint16_t val)
{
    taskENTER_CRITICAL();
    while (ISPI_TX_SPACE(REG_ISPI_STATUS) == 0);
    REG_ISPI_FIFO = ISPI_WRITE_CS1 | (((uint32_t)(reg) & 0x1FF) << 16) | (val & 0xFFFF);
    ispi_wait_idle();
    taskEXIT_CRITICAL();
}

/*
 * AIF (Audio Interface) MMIO Registers (0xA1927000)
 */
#define AIF_BASE              0xA1927000
#define REG_AIF_DATA          (*(volatile uint32_t *)(AIF_BASE + 0x00))
#define REG_AIF_CTRL          (*(volatile uint32_t *)(AIF_BASE + 0x04))
#define REG_AIF_SERIAL_CTRL   (*(volatile uint32_t *)(AIF_BASE + 0x08))
#define REG_AIF_TONE          (*(volatile uint32_t *)(AIF_BASE + 0x0C))
#define REG_AIF_SIDE_TONE     (*(volatile uint32_t *)(AIF_BASE + 0x10))
#define REG_AIF_LOAD_POS      (*(volatile uint32_t *)(AIF_BASE + 0x14))

/* SYS_CTRL clock divider for AIF (0xA1A00064) */
#define REG_SYS_CTRL_AIF_DIV  (*(volatile uint32_t *)0xA1A00064)

/*
 * ABB Register Addresses
 */
#define ABB_REG_CHIP_ID       0x00
#define ABB_REG_CODEC_SET_1   0x02
#define ABB_REG_CODEC_SET_2   0x03
#define ABB_REG_MODE_SEL      0x0C
#define ABB_REG_POWER_CTRL    0x0E
#define ABB_REG_CODEC_EN      0x129
#define ABB_REG_DAC_VOL       0x12A

static uint8_t s_volume = 12;
static bool    s_speaker_en = false;

void audio_init(void)
{
    /* 1. Enable AIF clock in SYS_CTRL: enable strobe (bit 16), update (bit 13), divider 128 */
    REG_SYS_CTRL_AIF_DIV = (1u << 16) | (1u << 13) | 128;

    /* 2. Configure AIF digital interface: master mode, load_pos 14, clear data/tone */
    REG_AIF_CTRL = 0;
    REG_AIF_SERIAL_CTRL = (1u << 4); /* Master mode */
    REG_AIF_LOAD_POS = 14;
    REG_AIF_TONE = 0;
    REG_AIF_SIDE_TONE = 0;
    REG_AIF_CTRL = 1; /* Enable AIF */

    /* 3. Configure ABB analog codec: mode sel, power, and clock */
    abb_write(ABB_REG_MODE_SEL, 0x8A00);
    abb_write(ABB_REG_POWER_CTRL, 0x0033); /* DAC enable, Codec reset deasserted */

    /* Enable digital codec logic: s_codec_en (bit 2) | s_dwa_en (bit 11) */
    abb_write(ABB_REG_CODEC_EN, 0x0804);

    /* Codec settings 1: controlled by ABB (bit 15), deep_pd_n (14), ref_pd_n (13), dac_pd_n (10), spk_sel (2) */
    abb_write(ABB_REG_CODEC_SET_1, (1u << 15) | (1u << 14) | (1u << 13) | (1u << 10) | (1u << 2));

    /* Codec settings 2: mono (bit 14), muted initially (bit 13 = 0), dac_reset_n (bit 12), gain */
    abb_write(ABB_REG_CODEC_SET_2, (1u << 14) | (0u << 13) | (1u << 12) | ((s_volume & 0x0F) << 8));

    /* DAC volume register: 0xFC00 top bits + volume code */
    abb_write(ABB_REG_DAC_VOL, 0xFC00 | (24 << 2));
    s_speaker_en = false;
}

uint16_t audio_get_chip_id(void)
{
    return abb_read(ABB_REG_CHIP_ID);
}

void audio_speaker_enable(bool en)
{
    s_speaker_en = en;
    uint16_t val = (1u << 14) | (1u << 12) | ((s_volume & 0x0F) << 8);
    if (en) {
        val |= (1u << 13); /* Unmute */
    }
    abb_write(ABB_REG_CODEC_SET_2, val);
}

bool audio_speaker_is_enabled(void)
{
    return s_speaker_en;
}

void audio_set_volume(uint8_t vol)
{
    if (vol > 15) vol = 15;
    s_volume = vol;
    audio_speaker_enable(s_speaker_en);
}

uint8_t audio_get_volume(void)
{
    return s_volume;
}

/*
 * Tone generation via AIF hardware synthesizer (AIF_TONE at 0xA192700C)
 *
 * tone_id 0..15:
 *   0..11: DTMF keys (0..9, *, #)
 *   12: 425 Hz dial / comfort tone
 *   13: 400 Hz tone
 *   14: 950 Hz alert tone
 *   15: 1 kHz high beep
 */
static const uint16_t k_tone_map[16] = {
    /* 0: DTMF '0' (941 Hz + 1336 Hz): row 3, col 1 */
    (3 << 6) | (1 << 4) | (0 << 1) | 1,
    /* 1: DTMF '1' (697 Hz + 1209 Hz): row 0, col 0 */
    (0 << 6) | (0 << 4) | (0 << 1) | 1,
    /* 2: DTMF '2' (697 Hz + 1336 Hz): row 0, col 1 */
    (0 << 6) | (1 << 4) | (0 << 1) | 1,
    /* 3: DTMF '3' (697 Hz + 1477 Hz): row 0, col 2 */
    (0 << 6) | (2 << 4) | (0 << 1) | 1,
    /* 4: DTMF '4' (770 Hz + 1209 Hz): row 1, col 0 */
    (1 << 6) | (0 << 4) | (0 << 1) | 1,
    /* 5: DTMF '5' (770 Hz + 1336 Hz): row 1, col 1 */
    (1 << 6) | (1 << 4) | (0 << 1) | 1,
    /* 6: DTMF '6' (770 Hz + 1477 Hz): row 1, col 2 */
    (1 << 6) | (2 << 4) | (0 << 1) | 1,
    /* 7: DTMF '7' (852 Hz + 1209 Hz): row 2, col 0 */
    (2 << 6) | (0 << 4) | (0 << 1) | 1,
    /* 8: DTMF '8' (852 Hz + 1336 Hz): row 2, col 1 */
    (2 << 6) | (1 << 4) | (0 << 1) | 1,
    /* 9: DTMF '9' (852 Hz + 1477 Hz): row 2, col 2 */
    (2 << 6) | (2 << 4) | (0 << 1) | 1,
    /* 10: DTMF '*' (941 Hz + 1209 Hz): row 3, col 0 */
    (3 << 6) | (0 << 4) | (0 << 1) | 1,
    /* 11: DTMF '#' (941 Hz + 1477 Hz): row 3, col 2 */
    (3 << 6) | (2 << 4) | (0 << 1) | 1,
    /* 12: Comfort tone 425 Hz (dial tone) */
    (2 << 8) | (1 << 1) | 1,
    /* 13: Comfort tone 400 Hz */
    (1 << 8) | (1 << 1) | 1,
    /* 14: Comfort tone 950 Hz (high alert) */
    (3 << 8) | (1 << 1) | 1,
    /* 15: DTMF high dual (852 Hz + 1633 Hz): row 2, col 3 */
    (2 << 6) | (3 << 4) | (0 << 1) | 1,
};

void audio_tone_play(uint8_t tone_id, uint8_t gain)
{
    if (tone_id > 15) tone_id = 15;
    if (gain > 3) gain = 3;

    /* Make sure AIF is enabled and speaker is unmuted */
    audio_speaker_enable(true);
    REG_AIF_CTRL = 1;

    uint32_t val = ((uint32_t)(gain & 0x03) << 12) | k_tone_map[tone_id];
    REG_AIF_TONE = val;
}

void audio_tone_stop(void)
{
    REG_AIF_TONE = 0;
}

void audio_beep(uint32_t duration_ms)
{
    uint32_t ms = duration_ms ? duration_ms : 400;
    uint32_t ticks = (ms + 5) / 10; /* 100 Hz tick: 1 tick = 10 ms */
    if (ticks == 0) ticks = 1;

    audio_tone_play(14, 3); /* 950 Hz tone, max gain */
    vTaskDelay(ticks);
    audio_tone_stop();
    audio_speaker_enable(false);
}
