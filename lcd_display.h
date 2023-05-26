#ifndef _LCD_DISPLAY_H
#define _LCD_DISPLAY_H

#include <stdio.h>
#include <stdbool.h>

bool open_lcd_dev();
bool close_lcd_dev();
void clear_screen();
void write_img_to_px_rect(unsigned char* img_buf, int img_px_w, int img_px_h, 
                      int scrn_px_x, int scrn_px_y, int scrn_px_w, int scrn_px_h);
void write_img_to_px_pos(unsigned char* img_buf, int img_px_w, int img_px_h, int scrn_px_x, int scrn_px_y);
bool write_img_file_to_px_pos(const char* img_file_name, int rect_x, int rect_y);
bool write_img_file_to_px_rect(const char* img_file_name,
                            int rect_x, int rect_y, int rect_w, int rect_h);

void all_scrn_px_on();
void all_scrn_px_off();
void exit_all_scrn_mode();

void print_bytes_arr(unsigned char * buf, ssize_t cnt, ssize_t row_num);
#define ARRAY_ITEM_CNT(a) sizeof(a)/sizeof((a)[0])

#endif
