/* 液晶模块型号：JLX25664G-580
串行接口
驱动 IC 是:ST75256
版权所有：晶联讯电子：网址 http://www.jlxlcd.cn;
*/
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <sys/param.h>

#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <linux/i2c-dev.h>
#include <errno.h>
#include <stdbool.h>
#include "lcd_display.h"

typedef unsigned char uchar; 
typedef unsigned int uint;

ssize_t write_img_into_col_pg_pos(uchar* buf, int d_col_bytes, int d_row_bytes,
                                         int col_s, int pg_s);
ssize_t write_img_into_col_pg_rect(uchar* buf, int d_col_bytes, int d_row_bytes,
                                         int col_s, int pg_s, int col_cnt, int pg_cnt);

static void test_1_dup_fit(int col_s, int pg_s, int column_num, int page_num, uchar d_byte)
{
    ssize_t w_cnt;
    int total_size = column_num * page_num;
    uchar * d_buf;

    printf("test_1_dup: col_s %d, pg_s %d, col num %d, page num %d, data 0x%X\n",
            col_s, pg_s, column_num, page_num, d_byte);

    if((column_num <= 0) || (page_num <= 0))
    {
        printf("Invalid column and page parm.\n");
        return;
    }
    d_buf = malloc(total_size);
    if(!d_buf)
    {
        printf("malloc error!\n");
        return;
    }
    memset(d_buf, d_byte, total_size);

    w_cnt = write_img_into_col_pg_rect(d_buf, column_num, page_num,
            col_s, pg_s, column_num, page_num);
    printf("test_1_dup, write data: %ld bytes.\n", w_cnt);

    free(d_buf);

    return;
}

static void test_2_dup_block(int d_w_bytes, int d_h_bytes, int col_s, int pg_s, int column_num, int page_num, uchar d_byte)
{
    ssize_t w_cnt;
    int total_size = d_w_bytes * d_h_bytes;
    uchar * d_buf;

    printf("test_1_dup: col_s %d, pg_s %d, col num %d, page num %d, data 0x%X\n",
            col_s, pg_s, column_num, page_num, d_byte);

    if((d_w_bytes <= 0) || (d_h_bytes <= 0))
    {
        printf("Invalid data width and height parm.\n");
        return;
    }
    d_buf = malloc(total_size);
    if(!d_buf)
    {
        printf("malloc error!\n");
        return;
    }
    memset(d_buf, d_byte, total_size);

    w_cnt = write_img_into_col_pg_rect(d_buf,d_w_bytes, d_h_bytes, 
            col_s, pg_s, column_num, page_num);
    printf("test_1_dup, write data: %ld bytes.\n", w_cnt);

    free(d_buf);

    return;
}

int main(int argc, char** argv)
{
    int d_w_bytes, d_h_bytes;
    int test_no;
    unsigned int d_byte;

    int r_col_s, r_col_cnt, r_pg_s, r_pg_cnt;
    bool lcd_ready = false, end;

    printf("Now begin lcd_test...\n");
    lcd_ready = open_lcd_dev();
    if(!lcd_ready)
    {
        printf("open_lcd_dev error!\n");
        return -1;
    }
    
    clear_screen();

    end = false;
    while(!end)
    {
        printf("input test no:\n");
        printf("0: clear screen.\n");
        printf("1: fill bytes block at ddram pos.\n");
        printf("2: fill bytes block of size at ddram pos.\n");
        printf("3: draw image on screen.\n");
        printf("4: draw image of size on screen.\n");
        printf("5: all pixel on.\n");
        printf("6: all pixel off.\n");
        printf("7: exit all screen on/off mode.\n");
        printf("-1: exit.\n");

        scanf("%d", &test_no);
        printf("\n");

        switch(test_no)
        {
            case 0:
                clear_screen();
                break;

            case 1:
            {
                printf("input write pos and data: col_s, pg_s, col_len, pg_len, d_byte:\n");
                scanf("%d %d %d %d %x", &r_col_s, &r_pg_s, &r_col_cnt, &r_pg_cnt, &d_byte);
                test_1_dup_fit(r_col_s, r_pg_s, r_col_cnt, r_pg_cnt, (uchar)d_byte);
            }
                break;

            case 2:
            {
                printf("input write pos and data: data_w, data_h, col_s, pg_s, col_len, pg_len, d_byte:\n");
                scanf("%d %d %d %d %d %d %x",
                    &d_w_bytes, &d_h_bytes, &r_col_s, &r_pg_s, &r_col_cnt, &r_pg_cnt, &d_byte);
                test_2_dup_block(d_w_bytes, d_h_bytes, r_col_s, r_pg_s, r_col_cnt, r_pg_cnt,
                                  (uchar)d_byte);
            }
                break;

            case 3:
            case 4:
            {
                const int max_file_name_len = 128;
                char img_file_name[max_file_name_len + 1];
                char fmt_str[32];
                int rect_x, rect_y, rect_w, rect_h;

                sprintf(fmt_str, "%%%ds", max_file_name_len);
                printf("please input the img file name:");
                scanf(fmt_str, img_file_name);

                if(3 == test_no)
                {
                    printf("please input the rect start pos x and y:");
                    scanf("%d%d", &rect_x, &rect_y);

                    write_img_file_to_px_pos(img_file_name, rect_x, rect_y);
                }
                else
                {
                    printf("please input the rect start pos x and y, and width and height:");
                    scanf("%d%d%d%d", &rect_x, &rect_y, &rect_w, &rect_h);

                    write_img_file_to_px_rect(img_file_name, rect_x, rect_y, rect_w, rect_h);
                }
            }
            break;

            case 5:
            {
                printf("turn on all pixels!\n");
                all_scrn_px_on();
            }
            break;

            case 6:
            {
                printf("turn off all pixels!\n");
                all_scrn_px_off();
            }
            break;

            case 7:
            {
                printf("exit all screen on/off mode.\n");
                exit_all_scrn_mode();
            }
            break;

            case -1:
                end = true;
                break;

            default:
                ;
        }

        printf("\n");
    }

    printf("close_lcd_dev\n");
    close_lcd_dev();
    printf("Exit lcd_test\n");
    return 0;
}
