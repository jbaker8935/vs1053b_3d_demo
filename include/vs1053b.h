#ifndef VS1053B_H
#define VS1053B_H

#include <stdint.h>
#include <stdbool.h>

/* K2 FPGA I/O addresses for VS1053b control and data registers */
#define VS_SCI_CTRL   0xD700
#define VS_SCI_ADDR   0xD701
#define VS_SCI_DATA   0xD702   /* 2 bytes */
#define VS_FIFO_COUNT 0xD704   /* 2 bytes */
#define VS_FIFO_DATA  0xD707

/* VS1053b specific SCI addresses */
#define VS_SCI_ADDR_MODE     0x00
#define VS_SCI_ADDR_STATUS   0x0001
#define VS_SCI_ADDR_BASS     0x0002
#define VS_SCI_ADDR_CLOCKF   0x0003
#define VS_SCI_ADDR_WRAM     0x0006
#define VS_SCI_ADDR_WRAMADDR 0x0007
#define VS_SCI_ADDR_VOL      0x000B

/* VS1053b CTRL modes */
#define CTRL_Start   0x01 /* start transfer */
#define CTRL_RWn     0x02 /* read mode */
#define CTRL_Busy    0x80 /* busy flag */

/* VS1053b SCI register addresses */
#define SCI_MODE        0x00
#define SCI_STATUS      0x01
#define SCI_VOL         0x0B  /* Volume: 0x0000=max, 0xFEFE=muted (~63.5dB), 0xFFFF=DAC off */
#define SCI_WRAMADDR    0x07
#define SCI_WRAM        0x06
#define SCI_AIADDR      0x0A
#define SCI_AICTRL0     0x0C
#define SCI_AICTRL1     0x0D
#define SCI_AICTRL2     0x0E
#define SCI_AICTRL3     0x0F

/* Memory space offsets for WRAMADDR */
#define IRAM_OFFSET     0x8000
#define XRAM_OFFSET     0x0000
#define YRAM_OFFSET     0x4000

/* VS1053 host helper API */
uint16_t vs1053_read_sci(uint8_t addr);
void vs1053_write_sci(uint8_t addr, uint16_t data);
void vs1053_write_mem(uint16_t wram_addr, uint16_t data);
uint16_t vs1053_read_mem(uint16_t wram_addr);

/* DAC / interrupt helpers */
void vs1053_mute_dac(void);
void vs1053_disable_dac_interrupt(void);
void vs1053_enable_dac_interrupt(void);

/* Plugin helpers */
void vs1053_plugin_init(uint16_t size_words);
void vs1053_plugin_load(void);
void vs1053_clock_boost(void);

#endif /* VS1053B_H */
