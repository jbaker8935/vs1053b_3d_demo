#ifndef CODEC_H
#define CODEC_H

#include <stdint.h>

/* Codec register identifiers */
#define CODEC_HPO_ATTN_LEFT 0x00
#define CODEC_HPO_ATTN_RIGHT 0x01
#define CODEC_HPO_ATTN_MSTR 0x02
#define CODEC_DAC_ATTN_LEFT 0x03
#define CODEC_DAC_ATTN_RIGHT 0x04
#define CODEC_DAC_ATTN_MSTR 0x05
#define CODEC_DAC_PHASE_CTRL 0x06
#define CODEC_DAC_CHL_CTL 0x07
#define CODEC_DAC_MUTE 0x08
#define CODEC_DAC_DEEMPH_CTL 0x09
#define CODEC_DAC_INTF_CTL 0x0A
#define CODEC_ADC_INTF_CTL 0x0B
#define CODEC_MSTR_MODE 0x0C
#define CODEC_PWR_DOWN_CTL 0x0D
#define CODEC_ADC_ATTN_LEFT 0x0E
#define CODEC_ADC_ATTN_RIGHT 0x0F
#define CODEC_ALC_CTL_1 0x10
#define CODEC_ALC_CTL_2 0x11
#define CODEC_ALC_CTL_3 0x12
#define CODEC_ALC_NOISE_GATE 0x13
#define CODEC_LIMITER_CTL 0x14
#define CODEC_ADC_MUX 0x15
#define CODEC_OUTPUT_MUX 0x16
#define CODEC_RESET 0x17

/* IO ports / control */
#define CODEC_CMD_LOW 0xD620
#define CODEC_CMD_HIGH 0xD621
#define CODEC_CTL_STATUS 0xD622
#define CODEC_START 0x01
#define CODEC_BUSY 0x01
#define CODEC_AIN1 0x01  /* SID and SAM2695 MIDI */
#define CODEC_AIN2 0x02  /* Line-in, OPL3 and MIDI Wave Table*/
#define CODEC_AIN3 0x04  /* PWM */
#define CODEC_AIN4 0x08  /* VS1053b */
#define CODEC_AIN5 0x10  /* Line-in onboard header */

/* Attenuation presets */
#define CODEC_DAC_ATTN_6DB (0x01FF - 2 * 6)  /* .5db per step */
#define CODEC_DAC_ATTN_9DB (0x01FF - 2 * 9)
#define CODEC_HPO_ATTN_0DB 0x0179
#define CODEC_HPO_ATTN_6DB (0x0179 - 6)
#define CODEC_HPO_ATTN_9DB (0x0179 - 9)
#define CODEC_HPO_ATTN_12DB (0x0179 - 12)

/* Channel control */
#define CODEC_DAC_CHL_CTL_STEREO 0x90
#define CODEC_DAC_CHL_CTL_MONO 0xF0

/* Public API */
void codec_write(uint16_t reg, uint16_t val);
void codec_init(void);

#endif /* CODEC_H */
