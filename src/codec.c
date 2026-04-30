#include "f256lib.h"
#include <stdint.h>
#include "../include/codec.h"

void codec_write(uint16_t reg, uint16_t val) {
    uint16_t data = (reg << 9) | (val & 0x01FF);
    POKE(CODEC_CMD_LOW, (uint8_t)(data & 0xFF));
    POKE(CODEC_CMD_HIGH, (uint8_t)((data >> 8) & 0xFF));
    POKE(CODEC_CTL_STATUS, CODEC_START);
    while (PEEK(CODEC_CTL_STATUS) & CODEC_BUSY)
        ;
}

void codec_channel_stereo_swap(bool swap) {
    uint16_t chl_ctl = PEEK(CODEC_DAC_CHL_CTL);
    if (swap) {
        chl_ctl = (chl_ctl & CODEC_DAC_CHL_CTL_OUT_MASK) | CODEC_DAC_CHL_CTL_SWAP;
    } else {
        chl_ctl = (chl_ctl & CODEC_DAC_CHL_CTL_OUT_MASK) | CODEC_DAC_CHL_CTL_STEREO;
    }
    codec_write(CODEC_DAC_CHL_CTL, chl_ctl);
}

/*  Codec Setup */
void codec_init(void) {
    codec_write(CODEC_ADC_MUX, CODEC_AIN2);
    /* Stereo output */
    codec_write(CODEC_DAC_CHL_CTL, CODEC_DAC_CHL_CTL_STEREO);
    /* Attenuate DAC by 6 dB for headroom */
    codec_write(CODEC_DAC_ATTN_MSTR, CODEC_DAC_ATTN_6DB);
    /* Attenuate headphone output by 6 dB */
    codec_write(CODEC_HPO_ATTN_MSTR, CODEC_HPO_ATTN_6DB);
}
