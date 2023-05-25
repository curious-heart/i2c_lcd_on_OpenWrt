import cv2 as cv
import numpy as np
import os
import argparse
import sys
import struct

usage_str = "img_data_generator.py file_or_dir [--direction row|col] [--bits_endian big|little]"

if __name__ == '__main__':
    file_list = []
    for f_or_d in sys.argv[1:]:
        if(os.path.isdir(f_or_d)):
            pt, _, f_l = list(os.walk(f_or_d))[0]
            for f in f_l:
                file_list.append([pt, f])
        else:
            file_list.append(['.', f_or_d])
    for it in file_list:
        img_base_name = os.path.splitext(it[1])[0]
        img_p_n = it[0] + '/' + it[1]
        img = cv.imread(img_p_n, cv.IMREAD_UNCHANGED)
        if(len(img.shape) > 2):
            img = img[:,:,0]
        img_mask = (img != 255)
        img[img_mask] = 1
        img[np.logical_not(img_mask)] = 0

        img_h, img_w = img.shape
        row_idx = 0
        row_bytes_arr = []
        while row_idx < img_h:
            if row_idx + 8 < img_h:
                bits_num = 8
            else:
                bits_num = img_h - row_idx
            row_bytes_arr += list(np.packbits(img[row_idx:row_idx + bits_num, :], 0, 'little')[0])
            row_idx += bits_num
        data_fpn = os.path.splitext(img_p_n)[0] + '_data'
        data_f = open(data_fpn, 'wb')
        w_h_info = struct.pack('<i', img_w)
        data_f.write(w_h_info)
        w_h_info = struct.pack('<i', img_h)
        data_f.write(w_h_info)
        for b in row_bytes_arr:
            b_d = struct.pack('B', b)
            data_f.write(b_d)
        data_f.close()

        c_fpn = data_fpn + '.c'
        c_lang_f = open(c_fpn, 'w')
        print('unsigned char {0}[] = {{'.format(img_base_name), file = c_lang_f)
        print_len = img_w if img_w < 16 else 16
        idx = 0
        for b in row_bytes_arr:
            print("0x{:02X}, ".format(b), end = '', file = c_lang_f)
            idx += 1
            if idx % print_len == 0: print("", file = c_lang_f)
        print('};', file = c_lang_f)
        c_lang_f.close()
