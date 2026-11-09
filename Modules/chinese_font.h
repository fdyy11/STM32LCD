/**
 ******************************************************************************
 * @file           : chinese_font.h
 * @brief          : 中文字库（16x16点阵）和ASCII字库（8x16点阵）
 * @note           : 包含常用汉字和ASCII字符
 ******************************************************************************
 */

#ifndef __CHINESE_FONT_H
#define __CHINESE_FONT_H

#include <stdint.h>

/* ASCII字库结构（8x16点阵） */
typedef struct {
    uint8_t ascii_code;    // ASCII 编码
    uint8_t data[16];      // 8x16 点阵数据（16字节）
} AsciiFont_t;

/* 中文字库结构 */
typedef struct {
    uint16_t unicode;      // Unicode 编码
    uint8_t data[32];      // 16x16 点阵数据（32字节）
} ChineseFont_t;

/* ASCII字库（8x16点阵） */
extern const AsciiFont_t ascii_font[];
extern const uint16_t ascii_font_count;

/* 常用中文字库（16x16点阵） */
extern const ChineseFont_t chinese_font[];
extern const uint16_t chinese_font_count;

/* 查找ASCII字库 */
const uint8_t* ascii_font_lookup(uint8_t ascii_code);

/* 查找中文字库 */
const uint8_t* chinese_font_lookup(uint16_t unicode);

#endif /* __CHINESE_FONT_H */
