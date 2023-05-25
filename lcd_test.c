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
#include "lcd_display.h""

typedef unsigned char uchar; 
typedef unsigned int uint;

/*We use the scrn as a monochrom one.*/

#define SCRN_PX_ROW_NUM 64 //pixels 
#define SCRN_PX_COL_NUM 256
#define DDRAM_MAX_COL_NUM SCRN_PX_COL_NUM 
#define DDRAM_MAX_PAGE_NUM 8 //(SCRN_PX_ROW_NUM / 8)
#define SCRN_BUF_BYTE_NUM 2048 //DDRAM_MAX_COL_NUM * DDRAM_MAX_PAGE_NUM 
//The local frame buffer is sychronized with display dram (DDRAM) 
static uchar gs_local_frame_buf[2048];


#define I2C_CMD_WITH_PARM_PREFIX_LEN 3
#define I2C_NON_RW_CMD_PARM_MAX_LEN 32
#define I2C_NON_RW_CMD_BUF_SIZE \
    (I2C_CMD_WITH_PARM_PREFIX_LEN + I2C_NON_RW_CMD_PARM_MAX_LEN)

#define I2C_DEV_MAX_READ_LEN 64
#define I2C_DEV_MAX_WRITE_LEN 64

#define I2C_W_CMD_BUF_SIZE I2C_DEV_MAX_WRITE_LEN
#define I2C_W_CMD_PARM_MAX_LEN \
    (I2C_W_CMD_BUF_SIZE - I2C_CMD_WITH_PARM_PREFIX_LEN) 

static unsigned char LCD_I2C_ADDR = 0x3C;

static int lcd_fd = -1;

static uchar I2C_LCD_CMD_CTRL_BYTE= 0x80;
static uchar I2C_LCD_CMD_PARM_CTRL_BYTE = 0x40;
static uchar I2C_LCD_DDRAM_DATA_CTRL_BYTE = 0x40;
//static uchar I2C_LCD_DDRAM_DATA_CTRL_BYTE = 0xC0;

static uchar I2C_LCD_CMD_ALL_PX_ON = 0x23;
static uchar I2C_LCD_CMD_ALL_PX_OFF = 0x22;
static uchar I2C_LCD_CMD_WRITE_DATA = 0x5C;
static uchar I2C_LCD_CMD_READ_DATA = 0x5D;
static uchar I2C_LCD_CMD_EXIT_PARTIAL_MODE = 0xA9;

typedef enum
{
    CASE_NONE = 0,
    CASE_1_LONG_DATA_ROW,
    CASE_2_MULTI_PG,
    CASE_3_COL_REMAINS,
}ddram_rw_case_t;

static void lcd_address(int x,int y,int x_total,int y_total);
static ssize_t transfer_command_lcd(uchar cmd, uchar* parm, ssize_t parm_len);

void print_bytes_arr(uchar * buf, ssize_t cnt, ssize_t row_num)
{
    ssize_t idx = 0;
    while(idx < cnt)
    {
        printf("0x%02X, ", buf[idx++]);
        if(idx % row_num == 0)
        {
            printf("\n");
        }

    }
    printf("\n");
}

static bool check_col_pg_addr(int col_s, int pg_s, int col_len, int pg_len)
{
    if((0 <= col_s && col_s < DDRAM_MAX_COL_NUM)
        && (1 <= col_len && col_len <= DDRAM_MAX_COL_NUM)
        && (1 <= col_s + col_len < DDRAM_MAX_COL_NUM)
        && (0 <= pg_s && pg_s < DDRAM_MAX_PAGE_NUM)
        && (1 <= pg_len && pg_len <= DDRAM_MAX_PAGE_NUM)
        && (1 <= pg_s + pg_len < DDRAM_MAX_PAGE_NUM))
    {
        return true;
    }
    else
    {
        return false;
    }
}

static bool check_col_pg_pos(int col_s, int pg_s)
{
    if((0 <= col_s && col_s < DDRAM_MAX_COL_NUM)
        && (0 <= pg_s && pg_s < DDRAM_MAX_PAGE_NUM))
    {
        return true;
    }
    else
    {
        return false;
    }
}

static bool clip_col_pg_rect(int col_s, int pg_s, int col_len, int pg_len,
                            int * clipped_col_len, int * clipped_pg_len)
{
    if(!check_col_pg_pos(col_s, pg_s) || (col_len < 0) || (pg_len < 0))
    {
        return false;
    }
    if(clipped_col_len)
    {
        (*clipped_col_len) 
            = (col_s + col_len >= DDRAM_MAX_COL_NUM) ? (DDRAM_MAX_COL_NUM - col_s) : col_len;
    }
    if(clipped_pg_len)
    {
        (*clipped_pg_len) 
            = (pg_s + pg_len >= DDRAM_MAX_COL_NUM) ? (DDRAM_MAX_COL_NUM - pg_s) : pg_len;
    }

    return true;
}

static bool check_scrn_px_pos(int x, int y)
{
    if((0 <= x && x < SCRN_PX_COL_NUM) && (0 <= y && y < SCRN_PX_ROW_NUM))
    {
        return true;
    }
    else
    {
        return false;
    }
}

static bool clip_scrn_px_rect(int x, int y, int w, int h, int * clipped_w, int * clipped_h)
{
    if(!check_scrn_px_pos(x, y) || (w < 0) || (h < 0))
    {
        return false;
    }
    if(clipped_w)
    {
        (*clipped_w) = (x + w >= SCRN_PX_COL_NUM) ? SCRN_PX_COL_NUM - x : w;
    }
    if(clipped_h)
    {
        (*clipped_h) = (y + h >= SCRN_PX_ROW_NUM) ? SCRN_PX_ROW_NUM - y : h;
    }

    return true;
}

static ssize_t lcd_read_io(void* buf, ssize_t cnt)
{
    ssize_t read_ret = 0;
    if(lcd_fd > 0)
    {
        read_ret = read(lcd_fd, buf, cnt);
        if(read_ret < 0)
        {
            printf("read error, errno %d, read %d\n", errno, read_ret);
        }
        if(0 == read_ret)
        {
            printf("read 0 bytes!\n");
        }

    }
    return read_ret;
}

static ssize_t lcd_write_io(void* buf, ssize_t cnt)
{
    ssize_t write_ret = 0;
    if(lcd_fd > 0)
    {
        write_ret = write(lcd_fd, buf, cnt);
        if(write_ret < 0)
        {
            printf("write errno: %d. written: %d\n", errno, write_ret);
        }
        if(0 == write_ret)
        {
            printf("write 0 bytes!\n");
        }
    }
    return write_ret;
}

/*buf contains img in 2D array arrange, and the data should be consecutive.*/
static ssize_t write_img_into_col_pg_rect(uchar* buf, int d_col_bytes, int d_row_bytes,
                                         int col_s, int pg_s, int col_cnt, int pg_cnt)
{
    uchar w_cmd_buf[I2C_W_CMD_BUF_SIZE];
    int w_idx, d_buf_row_idx, d_buf_col_idx, d_buf_row_idx_end;
    int clipped_col_cnt, clipped_pg_cnt;
    int area_pg_cnt, area_col_cnt;
    int remained_bytes;
    int col_ptr, this_cycle_col_cnt;
    int pg_ptr, this_cycle_pg_cnt;
    int this_cycle_written_bytes;
    ssize_t written_cnt = 0, write_ret;
    ssize_t actual_written_data;
    ddram_rw_case_t rw_type;

    printf("write_img_into_col_pg_rect:\n");
    printf("d_col_bytes %d, d_row_bytes %d, col_s %d, pg_s %d, col_cnt %d, pg_cnt %d\n",
           d_col_bytes, d_row_bytes, col_s, pg_s, col_cnt, pg_cnt);

    if(!clip_col_pg_rect(col_s, pg_s, col_cnt, pg_cnt, &clipped_col_cnt, &clipped_pg_cnt)
            || d_col_bytes < 0 || d_row_bytes < 0)
    {
        printf("Invalid pos and size parameters!\n");
        return 0;
    }
    printf("clipped_col_cnt: %d; clipped_pg_cnt: %d\n", clipped_col_cnt, clipped_pg_cnt);

    area_col_cnt = MIN(d_col_bytes, clipped_col_cnt);
    area_pg_cnt = MIN(d_row_bytes, clipped_pg_cnt);
    remained_bytes = area_col_cnt * area_pg_cnt;
    pg_ptr = pg_s; col_ptr = col_s;
    while(remained_bytes > 0)
    {
        w_idx = 0;
        w_cmd_buf[w_idx++] = I2C_LCD_CMD_CTRL_BYTE;
        w_cmd_buf[w_idx++] = I2C_LCD_CMD_WRITE_DATA;
        w_cmd_buf[w_idx++] = I2C_LCD_DDRAM_DATA_CTRL_BYTE;
        if(area_col_cnt - (col_ptr - col_s) > I2C_W_CMD_PARM_MAX_LEN)
        {
            rw_type = CASE_1_LONG_DATA_ROW;
            this_cycle_pg_cnt = 1;
            this_cycle_col_cnt = I2C_W_CMD_PARM_MAX_LEN;
        }
        else if(col_ptr == col_s)
        {
            rw_type = CASE_2_MULTI_PG;
            this_cycle_pg_cnt
                = (int)floor(MIN(remained_bytes, I2C_W_CMD_PARM_MAX_LEN)/ (area_col_cnt * 1.0));
            this_cycle_col_cnt = area_col_cnt;
        }
        else
        {
            rw_type = CASE_3_COL_REMAINS;
            this_cycle_pg_cnt = 1;
            this_cycle_col_cnt = area_col_cnt - (col_ptr - col_s) ;
        }
        this_cycle_written_bytes = this_cycle_pg_cnt * this_cycle_col_cnt;
        d_buf_col_idx = col_ptr - col_s;
        d_buf_row_idx = pg_ptr - pg_s;
        d_buf_row_idx_end = d_buf_row_idx + this_cycle_pg_cnt;
        while(d_buf_row_idx < d_buf_row_idx_end)
        {
            memcpy(&w_cmd_buf[w_idx], &buf[d_buf_row_idx * d_col_bytes + d_buf_col_idx],
                   this_cycle_col_cnt);
            ++d_buf_row_idx;
            w_idx += this_cycle_col_cnt;
        }
        printf("lcd_address: col_s %d, pg_s %d, col_cnt %d, pg_cnt %d.\n",
                col_ptr, pg_ptr, this_cycle_col_cnt, this_cycle_pg_cnt);
        print_bytes_arr(w_cmd_buf, w_idx, 16);
        lcd_address(col_ptr, pg_ptr, this_cycle_col_cnt, this_cycle_pg_cnt);
        write_ret = lcd_write_io(w_cmd_buf, w_idx);
        if(write_ret <= 0)
        {
            break;
        }
        actual_written_data = write_ret - I2C_CMD_WITH_PARM_PREFIX_LEN;
        if(actual_written_data <= 0)
        {
            printf("Written data bytes fail...\n");
            break;
        }
        switch(rw_type)
        {
            case CASE_1_LONG_DATA_ROW:
                col_ptr += actual_written_data;
                break;

            case CASE_2_MULTI_PG:
                pg_ptr += (int)floor(actual_written_data / (this_cycle_col_cnt * 1.0));
                col_ptr = col_s + (actual_written_data % this_cycle_col_cnt);
                break;

            default: //assuming CASE_3_COL_REMAINS
                pg_ptr += (int)floor(actual_written_data / (this_cycle_col_cnt * 1.0));
                col_ptr = col_s + (col_ptr - col_s + actual_written_data) % area_col_cnt;
        }
        this_cycle_written_bytes = actual_written_data;
        written_cnt += this_cycle_written_bytes;
        remained_bytes -= this_cycle_written_bytes;
    }

    return written_cnt;
}

/*buf contains img in 2D array arrange, and the data should be consecutive.*/
static ssize_t write_img_into_col_pg_pos(uchar* buf, int d_col_bytes, int d_row_bytes,
                                         int col_s, int pg_s)
{
    int col_cnt = DDRAM_MAX_COL_NUM - col_s;
    int pg_cnt = DDRAM_MAX_PAGE_NUM - pg_s;

    printf("write_img_into_col_pg_pos: d_col_bytes %d, d_row_bytes %d, col_s %d, pg_s %d\n",
           d_col_bytes, d_row_bytes,  col_s, pg_s);
    return write_img_into_col_pg_rect(buf, d_col_bytes, d_row_bytes,
            col_s, pg_s, col_cnt, pg_cnt);
}

//写指令到 LCD 模块
//non "write data" command
static ssize_t transfer_command_lcd(uchar cmd, uchar* parm, ssize_t parm_len)
{
    uchar cmd_str[I2C_NON_RW_CMD_BUF_SIZE];
    ssize_t cmd_len;
    cmd_str[0] = I2C_LCD_CMD_CTRL_BYTE;
    cmd_str[1] = cmd;
    cmd_len = 2;
    if(parm)
    {
        cmd_str[2] = I2C_LCD_CMD_PARM_CTRL_BYTE;
        cmd_len += 1;
        if(parm_len + I2C_CMD_WITH_PARM_PREFIX_LEN 
                > I2C_NON_RW_CMD_BUF_SIZE) 
        {
            parm_len = I2C_NON_RW_CMD_BUF_SIZE - I2C_CMD_WITH_PARM_PREFIX_LEN;
        }
        memcpy(&cmd_str[3], parm, parm_len);
        cmd_len += parm_len;
    }
    return lcd_write_io(cmd_str, cmd_len);
}

void initial_lcd()
{
    uchar cmd_parm[I2C_NON_RW_CMD_PARM_MAX_LEN];
    printf("enter initial...\n");

    ssize_t w_cnt = -1;
    ssize_t parm_len;
    transfer_command_lcd(0x30, NULL, 0); //EXT=0
    transfer_command_lcd(0x94, NULL, 0); //Sleep out
    transfer_command_lcd(0x31, NULL, 0); //EXT=1
                                
    cmd_parm[0] = 0x9F;
    transfer_command_lcd(0xD7, cmd_parm, 1); //Autoread disable
                           
    cmd_parm[0] = 0x00; //OSC Frequency adjustment
    cmd_parm[1] = 0x01; //Frequency on booster capacitors->6KHz
    cmd_parm[2] = 0x05; //Bias=1/9
    transfer_command_lcd(0x32, cmd_parm, 3); //Analog SET
                            
    parm_len = 3;
    memset(cmd_parm, 0x00, parm_len);
    memset(&cmd_parm[parm_len], 0x01, 3);
    parm_len += 3;
    memset(&cmd_parm[parm_len], 0x0, 2);
    parm_len += 2;
    cmd_parm[parm_len] = 0x1D;
    parm_len += 1;
    memset(&cmd_parm[parm_len], 0x0, 2);
    parm_len += 2;
    memset(&cmd_parm[parm_len], 0x1D, 3);
    parm_len += 3;
    memset(&cmd_parm[parm_len], 0x0, 2);
    parm_len += 2;
    transfer_command_lcd(0x20, cmd_parm, parm_len); //Gray Level

    transfer_command_lcd(0x30, NULL, 0); //EXT1=0，EXT0=0,表示选择了“扩展指令表 1”

    cmd_parm[0] = 0x00;    //起始页地址:YS
    cmd_parm[1] = 0x07;    //结束页地址:YE
    transfer_command_lcd(0x75, cmd_parm, 2); //页地址设置

    cmd_parm[0] = 0x00;    //起始col地址:YS
    cmd_parm[1] = 0xFF;    //结束col地址:YE
    transfer_command_lcd(0x15, cmd_parm, 2);//列地址设置

    cmd_parm[0] = 0x00;
    transfer_command_lcd(0xBC, cmd_parm, 1);//Data scan direction

    transfer_command_lcd(0xA6, NULL, 0); //Inverse Display:Normal
                               
    transfer_command_lcd(0x0C, NULL, 0); //Data Format Display:LSB on top

    cmd_parm[0] = 0x00;    //设置 CL 驱动频率：CLD=0
    cmd_parm[1] = 0x3F;//占空比：Duty=64
    cmd_parm[2] = 0x20;//N 行反显：Nline=off
    transfer_command_lcd(0xCA, cmd_parm, 3); //Dislay Control
    
    cmd_parm[0] = 0x10;//0x10: mono; 0x11: 4-gray scale
    transfer_command_lcd(0xF0, cmd_parm, 1);//Display Mode

    //设置对比度，“0x81”不可改动，紧跟着的 2 个数据是可改的，但“先微调后粗调”这个顺序别乱
    cmd_parm[0] = 0x12;//对比度微调，可调范围0x00～0x3f，共64 级
    cmd_parm[1] = 0x02;//对比度粗调，可调范围0x00～0x07，共 8 级
    transfer_command_lcd(0x81, cmd_parm, 2); //Set Vop
    
    cmd_parm[0] = 0x0B;//D0=regulator ; D1=follower ; D3=booste, on:1 off:0
    transfer_command_lcd(0x20, cmd_parm, 1); //Power Control
    
    printf("Open display.\n");
    transfer_command_lcd(0xAF, NULL, 0);//打开显示
                               //
    printf("exit initial...\n");
}
/*写 LCD 行列地址：X 为起始的列地址，Y 为起始的行地址，x_total,y_total 分别为列地址及行地址的起点到终点的差值 */
static void lcd_address(int x,int y,int x_total,int y_total)
{
    uchar cmd_parm[I2C_NON_RW_CMD_PARM_MAX_LEN];
    ssize_t w_cnt;

    cmd_parm[0] = x;
    cmd_parm[1] = x + x_total - 1;
    transfer_command_lcd(0x15, cmd_parm, 2);//Set Colomn Address

    cmd_parm[0] = y;
    cmd_parm[1] = y + y_total - 1;
    transfer_command_lcd(0x75, cmd_parm, 2);//Set Page Address
}

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
    printf("test_1_dup, write data: %d bytes.\n", w_cnt);

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
    printf("test_1_dup, write data: %d bytes.\n", w_cnt);

    free(d_buf);

    return;
}

/*清屏*/
void clear_screen()
{
    printf("Clear screen.\n");
    memset(gs_local_frame_buf, 0, sizeof(gs_local_frame_buf));
    write_img_into_col_pg_pos(gs_local_frame_buf, DDRAM_MAX_COL_NUM, DDRAM_MAX_PAGE_NUM, 0, 0);
}

bool open_lcd_dev()
{
    lcd_fd = open("/dev/i2c-0", O_RDWR);
    if(lcd_fd < 0)
    {
        return false;
    }
    else
    {
        return true;
    }
}

bool close_lcd_dev()
{
    int ret = close(lcd_fd);
    if(0 != ret)
    {
        printf("close_lcd_dev error: %d\n", ret);
        return false;
    }
    else
    {
        return true;
    }
}

int main(int argc, char** argv)
{
    int ret;
    int col_s, pg_s, column_num, page_num, d_w_bytes, d_h_bytes;
    int test_no;
    uchar d_byte;

    int r_col_s, r_col_cnt, r_pg_s, r_pg_cnt;
    ssize_t cmd_ret;
    bool end;

    printf("Now begin lcd_test...\n");
    open_lcd_dev();
    if(lcd_fd < 0)
    {
        printf("open_lcd_dev error!\n");
        return -1;
    }
    ret = ioctl(lcd_fd, I2C_SLAVE, LCD_I2C_ADDR);
    if(0 != ret)
    {
        printf("ioctl error:%d\n", ret);
        close_lcd_dev();
        return ret;
    }
    
    //对液晶模块进行初始化设置
    printf("initial_lcd\n");
    initial_lcd();

    clear_screen();

    end = false;
    while(!end)
    {
        printf("input tes no:\n");
        printf("0: clear screen.\n");
        printf("1: fill bytes block at ddram pos.\n");
        printf("2: fill bytes block of size at ddram pos.\n");
        printf("3: draw image on screen.\n");
        printf("4: draw image of size on screen.\n");
        printf("5: all pixel on.\n");
        printf("6: all pixel off.\n");
        printf("7: exit partial mode.\n");
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
                scanf("%d %d %d %d %d", &r_col_s, &r_pg_s, &r_col_cnt, &r_pg_cnt, &d_byte);
                test_1_dup_fit(r_col_s, r_pg_s, r_col_cnt, r_pg_cnt, d_byte);
            }
                break;

            case 2:
            {
                printf("input write pos and data: data_w, data_h, col_s, pg_s, col_len, pg_len, d_byte:\n");
                scanf("%d %d %d %d %d %d %d",
                    &d_w_bytes, &d_h_bytes, &r_col_s, &r_pg_s, &r_col_cnt, &r_pg_cnt, &d_byte);
                test_2_dup_block(d_w_bytes, d_h_bytes, r_col_s, r_pg_s, r_col_cnt, r_pg_cnt,
                                  d_byte);
            }
                break;

            case 3:
            case 4:
            {
                const int max_file_name_len = 128;
                char img_file_name[max_file_name_len + 1];
                char fmt_str[32];
                FILE* img_f;
                int px_wh[2];
                size_t fread_cnt, img_data_len;
                long file_pos;
                struct stat f_stat_buf;
                int f_ret;
                unsigned char *img_data;
                int rect_x, rect_y, rect_w, rect_h;

                sprintf(fmt_str, "%%%ds", max_file_name_len);
                printf("please input the img file name:");
                scanf(fmt_str, img_file_name);
                f_ret = stat(img_file_name, &f_stat_buf);
                if(f_ret < 0)
                {
                    printf("get info of file %s error, errno is %d.\n",
                            img_file_name, errno);
                    break;
                }
                printf("File size: %d\n", f_stat_buf.st_size);

                img_f = fopen(img_file_name, "rb");
                if(NULL == img_f)
                {
                    printf("Open file %s error.\n", img_file_name);
                    break;
                }
                fread_cnt = fread(px_wh, sizeof(int), 2, img_f);
                if(fread_cnt < 2)
                {
                    printf("fread file %s width-height error, %d.\n", img_file_name, fread_cnt);
                    fclose(img_f);
                    break;
                }
                printf("img width and height (in pixel): %d, %d\n",
                        px_wh[0], px_wh[1]);
                img_data_len = f_stat_buf.st_size - 2 * sizeof(int);
                if(((int)ceil(px_wh[1] / 8.0)) * px_wh[0] != img_data_len)
                {
                    printf("File size invlaid: img width and height does not match the size indicators at the first two int of the file.\n");
                    printf("float h: %f\n",(ceil(px_wh[1] / 8.0)));
                    printf("int h: %d\n",((int)ceil(px_wh[1] / 8.0)));
                    printf("shoul-be len: %d\n",((int)ceil(px_wh[1] / 8.0)) * px_wh[0]);
                    printf("img_data_len: %d\n", img_data_len);
                    fclose(img_f);
                    break;
                }

                img_data = malloc(img_data_len);
                if(NULL == img_data)
                {
                    printf("malloc error for file %s, size %lu + %lu.\n",
                            img_file_name, 2 * sizeof(int),  img_data_len); 
                }
                fread_cnt = fread(img_data, 1, img_data_len, img_f);
                printf("read image data %d bytes.\n", fread_cnt);
                if(fread_cnt != img_data_len)
                {
                    printf("read image data error.\n");
                    free(img_data);
                    fclose(img_f);
                    break;
                }

                if(3 == test_no)
                {
                    printf("please input the rect start pos x and y:");
                    scanf("%d%d", &rect_x, &rect_y);

                    write_img_to_pos(img_data, px_wh[0], px_wh[1], rect_x, rect_y);
                }
                else
                {
                    printf("please input the rect start pos x and y, and width and height:");
                    scanf("%d%d%d%d", &rect_x, &rect_y, &rect_w, &rect_h);
                    write_img_to_px_rect(img_data, px_wh[0], px_wh[1], 
                                          rect_x, rect_y, rect_w, rect_h);
                }

                free(img_data);
                fclose(img_f);
            }
            break;

            case 5:
            {
                printf("turn on all pixels!\n");
                cmd_ret = transfer_command_lcd(I2C_LCD_CMD_ALL_PX_ON, NULL, 0); 
                printf("write cmd ret: %d\n", cmd_ret);
            }
            break;

            case 6:
            {
                printf("turn off all pixels!\n");
                cmd_ret = transfer_command_lcd(I2C_LCD_CMD_ALL_PX_OFF , NULL, 0); 
                printf("write cmd ret: %d\n", cmd_ret);
            }
            break;

            case 7:
            {
                printf("exit partial mode.\n");
                cmd_ret = transfer_command_lcd(I2C_LCD_CMD_EXIT_PARTIAL_MODE, NULL, 0); 
                printf("write cmd ret: %d\n", cmd_ret);
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

/*
 * image and scrn are conunt on pixel.
 * Input an image with width and height of img_px_w and img_pw_h, this function write it
 * into screen rectangle (scrn_px_x, scrn_px_y, scrn_px_w, scrn_px_h). If image and screen 
 * area does not fit, only the overlapped part of image is displayed.
 *
 * img_buf conatains image data in consectutive bytes, row by row, from left to right,
 * from top to botom. Every byte is displayed on screen with LSB on top and MSB on bottom.
 * img_pw_h may not be times of 8, and if so, the higer bits are ignored.
 * 
 * */
void write_img_to_px_rect(unsigned char* img_buf, int img_px_w, int img_px_h, 
                      int scrn_px_x, int scrn_px_y, int scrn_px_w, int scrn_px_h)
{
    int clipped_scrn_px_w, clipped_scrn_px_h;
    int area_px_w, area_px_h;
    int fb_col_s, fb_pg_s, fb_col_len, fb_pg_len; //frame buffer rect

    if(!clip_scrn_px_rect(scrn_px_x, scrn_px_y, scrn_px_w, scrn_px_h, 
                     &clipped_scrn_px_w, &clipped_scrn_px_h)
            || (img_px_w <= 0) || (img_px_h <= 0))
    {
        return;
    }
    area_px_w = MIN(img_px_w, clipped_scrn_px_w);
    area_px_h = MIN(img_px_h, clipped_scrn_px_h);
    printf("img_px_w: %d, img_px_h: %d, scrn_px_x: %d, scrn_px_y: %d, scrn_px_w: %d, scrn_px_h: %d\n",
            img_px_w, img_px_h, scrn_px_x, scrn_px_y, scrn_px_w, scrn_px_h);
    printf("area_px_w: %d, area_px_h: %d\n", area_px_w, area_px_h);

    /*Write image into local frame buffer.*/
    fb_col_s = scrn_px_x;
    fb_col_len = area_px_w;
    fb_pg_s = (int)floor(scrn_px_y / 8.0);
    fb_pg_len = (int)ceil((scrn_px_y + area_px_h) / 8.0) - fb_pg_s;
    printf("fb_col_s: %d, fb_pg_s: %d, fb_col_len: %d, fb_pg_len: %d\n",
          fb_col_s, fb_pg_s, fb_col_len, fb_pg_len);

    int img_pg_len = (int)ceil(area_px_h / 8.0);
    int img_pg_idx, img_col_idx = 0;
    printf("img_pg_len: %d\n", img_pg_len);

    int fb_pg_idx = fb_pg_s, fb_col_idx = fb_col_s;
    int fb_px_y = scrn_px_y;

    if(scrn_px_y % 8 == 0)
    {
        img_pg_idx = 0;
        while(img_pg_idx < img_pg_len)
        {
            if(fb_px_y + 8 <= scrn_px_y + area_px_h)
            {
                memcpy(&gs_local_frame_buf[fb_pg_idx * SCRN_PX_COL_NUM + fb_col_s],
                       &img_buf[img_pg_idx * img_px_w],
                       area_px_w);
                ++fb_pg_idx;
                fb_px_y += 8;
                ++img_pg_idx;
            }
            else
            {
                int rem_bits_num = scrn_px_y + area_px_h - fb_px_y;
                uchar mask_img = 0xFF >> (8 - rem_bits_num);
                uchar mask_fb = ~mask_img; 
                for(img_col_idx = 0, fb_col_idx = fb_col_s; 
                    img_col_idx < area_px_w; ++img_col_idx, ++fb_col_idx)
                {
                    uchar img_b = mask_img & img_buf[img_pg_idx * img_px_w + img_col_idx];
                    uchar fb_b 
                        = mask_fb & gs_local_frame_buf[fb_pg_idx * SCRN_PX_COL_NUM + fb_col_idx];
                    gs_local_frame_buf[fb_pg_idx * SCRN_PX_COL_NUM + fb_col_idx]
                        = img_b | fb_b;
                }
                ++fb_pg_idx;
                fb_px_y += rem_bits_num;
                ++img_pg_idx;
            }
        }
    }
    else
    {
        int bot_rem_bits_num = 8 - scrn_px_y % 8;
        uchar bot_mask_img = 0xFF >> (8 - bot_rem_bits_num);
        uchar bot_mask_fb =  0xFF >> bot_rem_bits_num; 

        int top_rem_bits_num = 8 - bot_rem_bits_num;
        uchar top_mask_img = 0xFF << (8 - top_rem_bits_num);
        uchar top_mask_fb = 0xFF << top_rem_bits_num; 

        img_pg_idx = 0;
        while(fb_px_y < scrn_px_y + area_px_h)
        {
            if(fb_px_y + bot_rem_bits_num > scrn_px_y + area_px_h)
            {
                bot_rem_bits_num = scrn_px_y + area_px_h - fb_px_y;
                bot_mask_img = 0xFF >> (8 - bot_rem_bits_num);
                bot_mask_fb =  0xFF >> (8 - scrn_px_y % 8); 
                bot_mask_fb |= 0xFF << ((scrn_px_y % 8) + bot_rem_bits_num);
            }

            for(img_col_idx = 0, fb_col_idx = fb_col_s; 
                img_col_idx < area_px_w; ++img_col_idx, ++fb_col_idx)
            {
                uchar bot_img_b 
                    = bot_mask_img & img_buf[img_pg_idx * img_px_w + img_col_idx];
                bot_img_b = bot_img_b << (8 - bot_rem_bits_num);
                bot_img_b = bot_img_b >> (8 - (scrn_px_y % 8 + bot_rem_bits_num));

                uchar bot_fb_b 
                    = bot_mask_fb & gs_local_frame_buf[fb_pg_idx * SCRN_PX_COL_NUM
                                            + fb_col_idx];
                gs_local_frame_buf[fb_pg_idx * SCRN_PX_COL_NUM + fb_col_idx]
                    = bot_img_b | bot_fb_b;
            }

            fb_px_y += bot_rem_bits_num;

            printf("\n");
            ++fb_pg_idx;

            if(fb_px_y < scrn_px_y + area_px_h)
            {
                if(fb_px_y + top_rem_bits_num > scrn_px_y + area_px_h)
                {
                    top_rem_bits_num = scrn_px_y + area_px_h - fb_px_y;
                    top_mask_fb =  0xFF >> (8 - top_mask_img); 

                    top_mask_img = 0xFF << (8 - top_rem_bits_num);
                    top_mask_fb = 0xFF << (8 - top_mask_img);
                }

                for(img_col_idx = 0, fb_col_idx = fb_col_s; 
                    img_col_idx < area_px_w; ++img_col_idx, ++fb_col_idx)
                {
                    uchar top_img_b 
                        = top_mask_img & img_buf[img_pg_idx * img_px_w + img_col_idx];
                    top_img_b = top_img_b >> (8 - top_rem_bits_num);

                    uchar top_fb_b 
                        = top_mask_fb & gs_local_frame_buf[fb_pg_idx * SCRN_PX_COL_NUM
                                                           + fb_col_idx];
                    gs_local_frame_buf[fb_pg_idx * SCRN_PX_COL_NUM + fb_col_idx]
                        = top_img_b | top_fb_b;
                }
                fb_px_y += top_rem_bits_num;
            }

            ++img_pg_idx;
            printf("\n");
        }
    }

    printf("gs_local_frame_buf updted!\n");

    /*Collect the updated bytes in local frame buffer into a consecutive buffer.*/
    int to_ddram_bytes_num = fb_col_len * fb_pg_len;
    uchar* to_ddram_buf = malloc(to_ddram_bytes_num);
    if(!to_ddram_buf)
    {
        printf("malloc error!\n");
        return;
    }
    fb_pg_idx = fb_pg_s;
    int to_ddram_buf_idx = 0;
    while(fb_pg_idx < fb_pg_s + fb_pg_len)
    {
        memcpy(&to_ddram_buf[to_ddram_buf_idx],
               &gs_local_frame_buf[fb_pg_idx * SCRN_PX_COL_NUM + fb_col_s],
               fb_col_len);
        to_ddram_buf_idx += fb_col_len;
        ++fb_pg_idx;
    }

    /*Update the display dram.*/
    write_img_into_col_pg_pos(to_ddram_buf, fb_col_len, fb_pg_len, fb_col_s, fb_pg_s);

    free(to_ddram_buf);
}

/* This function write image at screen pos (scrn_px_x, scrn_px_y).
 * Refer to the comments of write_img_to_px_rect for the parameters and operation.
 * */
void write_img_to_pos(unsigned char* img_buf, int img_px_w, int img_px_h, int scrn_px_x, int scrn_px_y)
{
    int scrn_px_w = SCRN_PX_COL_NUM - scrn_px_x, scrn_px_h = SCRN_PX_ROW_NUM - scrn_px_y;
    write_img_to_px_rect(img_buf, img_px_w, img_px_h, scrn_px_x, scrn_px_y, scrn_px_w, scrn_px_h);
}
