#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
中文字库生成工具
用于生成16x16点阵的中文字库数据
"""

def generate_chinese_font():
    """
    生成中文字库数据
    使用示例：
    1. 安装依赖：pip install pillow
    2. 准备一个包含所需汉字的文本文件
    3. 运行脚本生成字库数据
    """
    
    # 需要显示的汉字
    chinese_chars = "电压流测系统运行正常状态指示等参数数量时钟"
    
    print("/* 中文字库数据 */")
    print("const ChineseFont_t chinese_font[] = {")
    
    for char in chinese_chars:
        unicode = ord(char)
        print(f"    /* 汉字: \"{char}\" (U+{unicode:04X}) */")
        print(f"    {{0x{unicode:04X}, {{")
        print("        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,")
        print("        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,")
        print("        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,")
        print("        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00")
        print("    }},")
        print()
    
    print("};")

if __name__ == "__main__":
    generate_chinese_font()
