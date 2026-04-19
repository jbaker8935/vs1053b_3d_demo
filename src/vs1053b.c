#include "f256lib.h"
#include <stdint.h>
#include "../include/vs1053b.h"

EMBED(plugin_data,"assets/plugin.bin",0x10000lu);

uint16_t vs1053_sci_read(uint8_t addr) {
    POKE(VS_SCI_ADDR, addr);
    POKE(VS_SCI_CTRL, CTRL_Start | CTRL_RWn);  /* Activate xCS and start read */
    POKE(VS_SCI_CTRL, 0);                      /* Deactivate xCS */

    while (PEEK(VS_SCI_CTRL) & CTRL_Busy)
        ;
    uint16_t ret = ((uint16_t)PEEK(VS_SCI_DATA + 1) << 8) | PEEK(VS_SCI_DATA);

    return ret;
}

void vs1053_sci_write(uint8_t addr, uint16_t data) {
    POKE(VS_SCI_ADDR, addr);
    POKEW(VS_SCI_DATA, data);
    POKE(VS_SCI_CTRL, CTRL_Start);  /* start write */
    POKE(VS_SCI_CTRL, 0);           /* deactivate */
    while (PEEK(VS_SCI_CTRL) & CTRL_Busy)
        ;
    return;
}

void vs1053_mem_write(uint16_t wram_addr, uint16_t data) {
    vs1053_sci_write(SCI_WRAMADDR, wram_addr);
    vs1053_sci_write(SCI_WRAM, data);
}

uint16_t vs1053_mem_read(uint16_t wram_addr) {
    vs1053_sci_write(SCI_WRAMADDR, wram_addr);
    return vs1053_sci_read(SCI_WRAM);
}
__attribute__((noinline))
void vs1053_dac_mute(void) {
    /* Mute both channels (~63.5 dB attenuation) */
    vs1053_sci_write(SCI_VOL, 0xFEFE);
}
__attribute__((noinline))
void vs1053_dac_interrupt_disable(void) {
    uint16_t reg = vs1053_mem_read(INT_ENABLE);      /* INT_ENABLE read */
    reg &= ~(INT_EN_DAC);                           /* clear INT_EN_DAC */
    vs1053_mem_write(INT_ENABLE, reg);
}
__attribute__((noinline))
void vs1053_dac_interrupt_enable(void) {
    uint16_t reg = vs1053_mem_read(INT_ENABLE);
    reg |= INT_EN_DAC;                            /* set INT_EN_DAC */
    vs1053_mem_write(INT_ENABLE, reg);
}

/* -----------------------------------------------------------------------
 * Plugin load/clock helpers
 * ----------------------------------------------------------------------- */
__attribute__((noinline))
 void vs1053_plugin_init(uint16_t size) {
  uint16_t n;
  uint16_t addr, val;
  uint32_t i = 0;
  /* i is the byte offset into the plugin data; size is in words */
  while (i < (uint32_t)size * 2u) {
    addr = FAR_PEEKW(0x10000lu + (uint32_t)i);
    n = FAR_PEEKW(0x10000lu + (uint32_t)(i + 2u));
    i += 4u;

    if (n & 0x8000U) { /* RLE run, replicate n samples */
      n &= 0x7FFF;
      val = FAR_PEEKW(0x10000lu + (uint32_t)i);
      i += 2u;
      while (n--) {
        vs1053_sci_write(addr,val);
      }
    } else {
      /* Copy run, copy n samples */
      while (n--) {
        val = FAR_PEEKW(0x10000lu + (uint32_t)i);
        i += 2u;
        vs1053_sci_write(addr,val);
      }
    }
  }
}
__attribute__((noinline))
void vs1053_plugin_load() {
  uint32_t plugin_size_bytes = (uint32_t)(plugin_data_end - plugin_data_start);
  uint16_t plugin_size_words = (uint16_t)(plugin_size_bytes >> 1);
  vs1053_plugin_init(plugin_size_words);
}

void vs1053_clock_boost(uint16_t mult, uint16_t add) {
  /* Recommended SC_MULT=3.5x/SC_ADD=1.0x (SCI_CLOCKF=0x8800) */
  /* SC_MULT=4.5x/SC_ADD=0x0 (SCI_CLOCKF=0xC000) */
  vs1053_sci_write(VS_SCI_ADDR_CLOCKF, mult | add);
}
