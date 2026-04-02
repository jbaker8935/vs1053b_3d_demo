#if !defined(DRAW_LINE_H)
#define DRAW_LINE_H
#include <stdint.h>
// limit line list to 36 to match max edges output by the kernel
#define MAX_LINE_LIST 36

/* 
 * line_list is a structure for accumulating line segments from the kernel for
 * display by draw_lines_asm.  Currently only used by scene mode rendering.
 */
struct line_list {
    uint16_t x0[MAX_LINE_LIST];
    uint16_t x1[MAX_LINE_LIST];
    uint16_t y[MAX_LINE_LIST];  // combined y0 and y1: y0 = low byte, y1 = high byte
    uint16_t color[MAX_LINE_LIST];  // low byte is color
};

extern struct line_list g_line_list;
extern uint8_t __zp g_line_count;  // 0 to MAX_LINE_LIST(36)
extern uint8_t g_line_count_max;   // peak line count per frame (diagnostic)

void reset_line_list(void);
void add_line_to_list(uint16_t x0, uint8_t y0, uint16_t x1, uint8_t y1, uint8_t color);

extern uint16_t g_line_dropped; // count lines that couldn't be added due to list full

void draw_lines_from_list(uint8_t layer);
void draw_line(uint16_t x0, uint8_t y0, uint16_t x1, uint8_t y1, uint8_t color, uint8_t layer);

extern void draw_lines_asm(uint8_t layer);

#endif /* DRAW_LINE_H */
