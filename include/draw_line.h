#if !defined(DRAW_LINE_H)
#define DRAW_LINE_H

// Hardware Registers

#define DL_CONTROL 0xD180  // layer<<2|1 = enable; |0x02 = clock; 0x00 = disable
#define DL_COLOR   0xD181
#define DL_X0      0xD182
#define DL_X1      0xD184
#define DL_Y       0xD186
#define DL_MODE    0xD00A
#define DL_FIFO_LO 0xD182
#define DL_FIFO_HI 0xD183

#endif /* DRAW_LINE_H */
