#ifndef _LCD_DISPLAY_H
#define _LCD_DISPLAY_H


bool open_lcd_dev();
bool close_lcd_dev();
void clear_screen();
void write_img_to_px_rect(unsigned char* img_buf, int img_px_w, int img_px_h, 
                      int scrn_px_x, int scrn_px_y, int scrn_px_w, int scrn_px_h);
void write_img_to_pos(unsigned char* img_buf, int img_px_w, int img_px_h, int scrn_px_x, int scrn_px_y);

#endif
