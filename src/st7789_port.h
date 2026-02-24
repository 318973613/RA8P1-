#ifndef ST7789_PORT_H
#define ST7789_PORT_H

#include <stdint.h>

int st7789_init(void);
void st7789_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);
int st7789_write_pixels(const void *data, uint32_t length);
int st7789_draw_text(int x, int y, const char *text, uint16_t color, uint16_t bg, int scale);
int st7789_hello(void);

void lcd_write_cmd(uint8_t cmd);
void lcd_write_data(uint8_t data);
void lcd_fill(uint16_t color);

#endif
