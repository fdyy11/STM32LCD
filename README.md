# STM32F103RCT6 项目开发指南

> **项目简介**：基于CMake构建系统的STM32F103RCT6嵌入式开发项目，支持ST7789 LCD显示、串口通讯和多种烧录方式。

---

## 📋 目录

- [硬件配置](#硬件配置)
- [环境搭建](#环境搭建)
- [编译与烧录](#编译与烧录)
- [LCD显示功能](#lcd显示功能)
- [RS485电池通讯](#rs485电池通讯)
- [串口通讯功能](#串口通讯功能)
  - [功能概述](#功能概述)
  - [硬件连接](#硬件连接)
  - [使用方法](#使用方法)
  - [API接口](#api-接口)
  - [代码示例](#代码示例)
  - [显示逻辑](#显示逻辑)
- [LED指示功能](#led指示功能)
- [故障排查](#故障排查)
- [版本历史](#版本历史)

---

## 🔧 硬件配置

### 1. ST7789 LCD 模块接线（ATM0240B32）

| LCD引脚 | STM32引脚 | 功能说明 |
|---------|-----------|----------|
| VCC     | 3.3V      | 电源正极 |
| GND     | GND       | 电源地   |
| SCL     | **PB3**   | SPI时钟信号 |
| SDI     | **PB5**   | SPI数据输入(MOSI) |
| SDO     | PB4       | SPI数据输出(MISO，可选) |
| CS      | **PA12**  | 片选信号 |
| D/CX    | **PB6**   | 数据/命令选择 |
| RST     | **PA15**  | 复位信号 |
| LEDK    | **PB7**   | 背光控制 |

### 2. LED 指示灯接线

| LED引脚 | STM32引脚 | 功能说明 |
|---------|-----------|----------|
| LED+    | **PB1**   | LED正极（高电平点亮） |
| LED-    | GND       | 通过220Ω-1kΩ电阻接地 |

> ⚠️ **注意**：如果开发板已有板载LED连接到PB1，则无需外接。

### 3. ST-Link V2 烧录器接线（推荐）

| ST-Link引脚 | STM32引脚 | 必需性 |
|------------|-----------|--------|
| SWCLK      | **PA14**  | ✅ 必需 |
| SWDIO      | **PA13**  | ✅ 必需 |
| GND        | GND       | ✅ 必需 |
| 3.3V       | 3.3V      | ⚠️ 建议（如开发板无独立供电） |
| NRST       | NRST      | ❌ 可选 |

> ⚠️ **重要**：先接GND，再接信号线！

### 4. USB-TTL 串口烧录接线（备选）

| USB-TTL引脚 | STM32引脚 | 说明 |
|------------|-----------|------|
| TX         | **PA10** (RX) | 交叉连接 |
| RX         | **PA9** (TX)  | 交叉连接 |
| GND        | GND       | 共地 |

**ISP模式设置**：
- **烧录前**：BOOT0 = HIGH (3.3V), BOOT1 = LOW (GND)
- **烧录后**：BOOT0 = LOW (GND), BOOT1 = LOW (GND)

### 5. UART4 WiFi串口（PC10/PC11 固定引脚）

| USB-TTL引脚 | STM32引脚 | 说明 |
|------------|-----------|------|
| RX         | **PC10** (TX) | 交叉连接 |
| TX         | **PC11** (RX) | 交叉连接 |
| GND        | GND       | 共地 |

> PC10/PC11 是 UART4 的固定引脚：PC10 是 TX，PC11 是 RX。当前固件默认按 115200bps、8N1 配置 UART4 用于WiFi模块通讯；具体波特率可在 `wifi_uart4.h` 的 `WIFI_UART_BAUD_RATE` 宏中修改。

### 6. Pylon 电池 RS485 接线

| TTL-RS485 模块 | STM32F103RCT6 | 说明 |
|---------------|---------------|------|
| DI / TXD | **PB10** | USART3 默认 TX 引脚 |
| RO / RXD | **PB11** | USART3 默认 RX 引脚 |
| GND | GND | 必须共地 |
| A / B | 电池 RS485 A / B | A 对 A、B 对 B；无响应时检查是否需要交换 |

固件按协议文档使用 **9600 bps、8N1**，并按默认单组地址先查询 `0x12`，无响应再尝试 `0x02`。如果电池 DIP 配置了其他组地址，在 `battery_rs485.h` 中设置 `BATTERY_GROUP_ADDRESS`（0–7），修改后重新编译。电池RS485使用 USART3 的 PB10/PB11 引脚，PB13 作为 DE/RE 收发方向控制（高电平发送、低电平接收）；WiFi使用 UART4 的 PC10/PC11 固定引脚，两路互不占用。

> **接线安全**：确认模块 TTL 侧为 3.3V 电平，并只连接电池的 RS485 端口，不要连接 CAN 口。
>
> 协议文件中 ADR=`0x02` 的示例校验和与其给出的 ASCII 求和算法不一致；固件按校验和算法生成该请求。

### 7. 按键接线和开屏

| 按键 | STM32引脚 | 功能 |
|------|-----------|------|
| Esc | **PC0** | 退出开屏并进入第一页；在其他页面返回第一页 |
| PageUp | **PC2** | 上一页 |
| PageDown | **PC3** | 下一页 |

按键引脚配置为内部上拉输入，按键另一端接 GND，按下时为低电平。上电后显示白底、金红配色 Logo，直到按下 Esc 才进入第一页；开屏期间电池通信继续运行，翻页键不切换页面。

开屏图片数据位于 `Modules/startup_logo.c`，尺寸和接口声明位于 `Modules/startup_logo.h`。更换图片时需同步更新这两个文件及 CubeMX 工程中的对应文件。

---

## 💻 环境搭建

### 必需工具

1. **CMake** ≥ 3.20
2. **ARM GCC 工具链** (arm-none-eabi-gcc)
3. **烧录工具**（二选一）：
   - st-flash（推荐，快速简单）
   - stm32flash（串口烧录）

### 工具安装路径（推荐）

```
D:\Tools\ARM_GCC\arm-gnu-toolchain-13.2.Rel1-mingw-w64-i686-arm-none-eabi\bin
D:\Tools\stlink
D:\Tools\CMake\bin
```

### 配置环境变量

```powershell
# 临时添加（当前会话有效）
$env:Path = "D:\Tools\ARM_GCC\arm-gnu-toolchain-13.2.Rel1-mingw-w64-i686-arm-none-eabi\bin;" + $env:Path
$env:Path = "D:\Tools\stlink;" + $env:Path

# 永久添加（需要管理员权限）
[Environment]::SetEnvironmentVariable("Path", 
    (Get-ItemProperty -Path "HKCU:\Environment").Path + ";D:\Tools\ARM_GCC\arm-gnu-toolchain-13.2.Rel1-mingw-w64-i686-arm-none-eabi\bin;D:\Tools\stlink", "User")
```

### 验证安装

```powershell
# 检查 ARM GCC
arm-none-eabi-gcc --version

# 检查 st-flash
st-flash --version

# 查看可用串口
[System.IO.Ports.SerialPort]::GetPortNames() | Sort-Object
```

---

## 🚀 编译与烧录

### 方法1：ST-Link 烧录（推荐）⭐

#### 优势
- ✅ 无需设置BOOT跳线
- ✅ 支持在线调试
- ✅ 烧录速度快（2-5秒）
- ✅ 稳定性高

#### 步骤

**1. 硬件连接**
按上述接线图连接ST-Link V2到STM32。

**2. 编译固件**
```powershell
cmake --build build/Debug
```

**3. 执行烧录**
```
# 方式A：使用智能脚本
.\flash_stlink.ps1

# 方式B：使用CMake目标
cmake --build build/Debug --target flash_stlink_fast

# 方式C：直接使用st-flash
st-flash --reset write build/Debug/stm32_1.bin 0x08000000

# 方式D：使用STM32CubeProgrammer（需连接ST-Link）
cmake --build build/Debug --target flash_stlink_cube
```

`flash_stlink_cube` 会优先从 PATH 查找 STM32CubeProgrammer；在 Windows 上也会自动查找 `%LOCALAPPDATA%\stm32cube\bundles\programmer` 下的已安装版本。

#### 预期输出
```
========================================
  ST-Link V2 Flash Tool
========================================

Flash file: build/stm32_1.bin (Binary)
File size: 2.93 KB

Starting flash process...
Using st-flash (fast mode)...
st-flash 1.9.0
2026-10-08T16:00:00 INFO common.c: Device connected is: F1 Medium-density device
2026-10-08T16:00:02 INFO common.c: Flash written and verified! jolly good!

========================================
  Flash Successful!
========================================
```

---

### 方法2：串口烧录（备选）

#### 步骤

**1. 硬件连接**
- 连接USB-TTL到STM32（TX→PA10, RX→PA9, GND→GND）
- 设置BOOT0=HIGH, BOOT1=LOW进入ISP模式

**2. 编译固件**
```powershell
cmake --build build/Debug
```

**3. 执行烧录**
```
# 方式A：使用智能脚本
.\flash_stm32.ps1 -Port COM3

# 方式B：指定波特率（如遇问题可降低）
.\flash_stm32.ps1 -Port COM3 -BaudRate 57600

# 方式C：使用CMake目标
cmake --build build/Debug --target flash
```

**4. 恢复正常运行**
- 断电后将BOOT0改为LOW
- 重新上电运行程序

---

## 🔍 LEDK引脚详解

### **ATM0240B32背光控制引脚：**

| 引脚 | 全称 | 功能 | 电平要求 |
|------|------|------|----------|
| **LEDA** | LED Anode | 背光阳极（正极） | 接3.3V |
| **LEDK** | LED Kathode | 背光阴极（负极） | **接地或低电平点亮** |

### **工作原理：**

```
LEDA (3.3V) ──→ [LED背光] ──→ LEDK (GND/LOW)
                         ↑
                    电流回路形成，背光亮起
```

**控制逻辑**：
- ✅ **LEDK = LOW (GND)** → 背光**亮起**
- ❌ **LEDK = HIGH (3.3V)** → 背光**关闭**
- ❌ **LEDK = 悬空** → 背光**不亮或很暗**

---

## ✅ v3.0 解决方案

### **方案对比：**

| 方案 | 优点 | 缺点 | 推荐度 |
|------|------|------|--------|
| **硬件直连** | 简单可靠 | 无法软件控制 | ⭐⭐⭐ |
| **PB7软件控制** | 灵活可控 | 需要代码支持 | ⭐⭐⭐⭐⭐ |

---

### **方案1：硬件直接连接（快速测试）**

**接线方法**：
```
STM32 GND ──→ LCD LEDK
STM32 3.3V ──→ LCD LEDA
```

**效果**：背光常亮，无需代码修改

**适用场景**：
- 快速验证LCD是否正常
- 不需要动态控制背光亮度
- 调试阶段临时使用

---

### **方案2：PB7软件控制（推荐）** ⭐⭐⭐⭐⭐

**已实现的功能**：

#### **1. 硬件连接**
```
STM32 PB7 ──→ LCD LEDK
STM32 3.3V ──→ LCD LEDA
```

#### **2. 代码实现**

在`main.c`中添加了PB7背光控制：

```c
// Configure PB7 as push-pull output (LCD Backlight control)
GPIOB_CRL &= ~(0xF << 28);  // Clear PB7 configuration
GPIOB_CRL |=  (0x3 << 28);  // Set PB7 as output 50MHz

// Turn ON LCD backlight (PB7 LOW = LEDK grounded)
GPIOB_ODR &= ~(1 << 7);     // PB7 LOW to enable backlight
```

**控制逻辑**：
- `GPIOB_ODR &= ~(1 << 7)` → PB7输出**低电平** → LEDK接地 → **背光亮起** ✅
- `GPIOB_ODR |= (1 << 7)` → PB7输出**高电平** → LEDK接3.3V → **背光关闭** ❌

#### **3. 扩展功能（可选）**

可以实现PWM调光：

```c
// PWM调光示例（需配置定时器）
void LCD_Set_Backlight_Brightness(uint8_t duty_cycle) {
    // duty_cycle: 0-100 (0% = 灭, 100% = 最亮)
    // 通过PWM控制PB7占空比实现调光
}
```

---

## 🎨 LCD显示功能

### 初始化与基本显示

> **已验证配置（不要随意删除）**
>
> `PA15`、`PB3` 和 `PB4` 是 STM32F103 的调试端口复用引脚：复位状态下默认 JTAG/SWD 配置可能占用它们。当前 LCD 使用的软件 SPI 和复位引脚为：
>
> - `PA15`：LCD RST
> - `PB3`：LCD SCK
> - `PB4`：LCD MISO/未使用
>
> 因此 [LCD_GPIO_Init](./Modules/lcd_st7789.c) 必须启用 AFIO，并只禁用 JTAG-DP：
>
> ```c
> RCC->APB2ENR |= (1u << 0);       // 使能 AFIO
> AFIO->MAPR = (AFIO->MAPR & ~AFIO_MAPR_SWJ_CFG) |
>              AFIO_MAPR_SWJ_CFG_JTAGDISABLE;
> ```
>
> `SWJ_CFG=010` 保留 `PA13/SWDIO` 和 `PA14/SWCLK`，可继续使用 ST-Link 多次烧录。不要改成 `SWJ_CFG=100`（禁用全部 SWJ），否则固件运行后可能无法继续通过 SWD 连接。
>
> AFIO配置只允许执行一次，由 [LCD_GPIO_Init](./Modules/lcd_st7789.c) 统一设置 `SWJ_CFG=010`。USART3 使用 PB10/PB11 默认复用引脚，因此初始化中不得再次读取或写入 `AFIO->MAPR`；UART4 使用固定的 PC10/PC11，也不应改写 AFIO。不要在 `SystemInit()`、USART3初始化和周期任务中重复写入 SWJ_CFG。

---

### 软件 SPI 快速翻页

- 当前 Debug 配置保留调试符号并使用 `-O0`，不是本项目中识别和解决 LCD/SWD 冲突的主要原因。
- 区域填充使用与 LCD 命令和字符显示相同的 `SOFT_SPI_WriteData()` 时序，不再使用展开 GPIO 序列的高速实现。
- 软件 SPI 先在 SCK 下降沿设置 MOSI，再单独拉高 SCK，保持模式 0 的数据建立时间；每个时钟周期写两次 BSRR。
- 区域填充结束时将 SCK 恢复为低电平，确保下一条 LCD 命令的首位也有完整时钟沿。
- 上一页和下一页使用同一全屏快速刷新路径，启动时也只清屏一次。

翻页实际耗时受 MCU 主频、屏幕模块和连线影响，应以目标硬件实测为准。

## 📡 双串口通讯功能

当前固件同时保留两路独立串口：

- **USART3（PB10/PB11，9600 bps）**：电池 TTL-RS485 协议通讯
- **UART4（PC10/PC11，115200 bps）**：WiFi模块通讯

UART4 接收数据通过中断写入256字节环形缓冲区，可使用 `WiFi_UART4_ReadBytes()`读取；发送可使用 `WiFi_UART4_SendBytes()`或 `WiFi_UART4_SendString()`。

**2. 发送数据到STM32**

使用串口调试助手（如SSCOM、XCOM等）连接到USB-TTL模块，发送数据：

```
发送格式：字符串 + 回车换行（\r\n）

示例：
Hello STM32!\r\n
Line 2 Test\r\n
Line 3 Test\r\n
```

**3. 查看显示结果**

1. 烧录程序后，LCD屏幕开始运行
2. 使用按键切换到第5页（Page 5/5）
3. 串口接收到的数据将显示在第5页
4. 最多显示5行，超出后从第1行开始覆盖

**4. 验证串口回显**

使用串口调试助手发送任意数据，例如：
```
Hello STM32!\r\n
```

**预期结果**：
- 串口调试助手会收到STM32返回的相同数据：`Hello STM32!\r\n`
- LCD屏幕第5页会显示该数据
- 如果返回的数据与发送的数据一致，说明串口收发功能正常 ✅

### API 接口

| 函数 | 说明 | 示例 |
|------|------|------|
| `usart3_init(bound)` | 初始化USART3 | `usart3_init(115200);` |
| `usart3_send_byte(byte)` | 发送一个字节 | `usart3_send_byte('A');` |
| `usart3_send_string(str)` | 发送字符串 | `usart3_send_string("Hello");` |
| `usart3_send_debug_info()` | 发送调试信息 | `usart3_send_debug_info();` |

### 代码示例

**发送数据到串口**：

```c
// 方式1：发送字符串
usart3_send_string("Hello STM32!\r\n");

// 方式2：使用printf（已支持）
printf("Temperature: %d.%d°C\r\n", 25, 5);

// 方式3：发送单个字节
usart3_send_byte(0x41);  // 发送字符'A'
```

**接收数据处理**：

```c
// 自动接收：中断方式接收数据
// USART3_RX_BUF[]: 接收缓冲区
// USART3_RX_STA: 接收状态标志
// USART3_RX_COMPLETE: 接收完成标志

// 主循环中自动调用
parse_serial_data();  // 解析接收到的数据
display_serial_data(); // 显示到LCD第5页
```

### 显示逻辑

```
第5页显示效果（最多5行）：
┌────────────────────────────────────┐
│ Line 01: Hello STM32!              │
│ Line 02: USART3 Test               │
│ Line 03: Data received             │
│ Line 04: Display on page 5         │
│ Line 05: Overwrite from line 1     │ ← 超出后覆盖
└────────────────────────────────────┘
```

**工作流程**：

```
graph TD
    A[串口调试助手发送数据] --> B[USART3接收中断]
    B --> C[数据存入缓冲区]
    C --> D[设置接收完成标志]
    D --> E[主循环检测标志]
    E --> F[解析数据行]
    F --> G[回显数据到串口]
    G --> H{是否在第5页?}
    H -->|是| I[刷新显示]
    H -->|否| J[继续等待]
    I --> K[清空接收标志]
    J --> K
    K --> L[等待下次接收]
```

**串口回显功能说明**：
- 当STM32接收到串口数据后，会立即将数据原样返回
- 这样可以在串口调试助手中看到发送的数据被回显
- 如果回显数据与发送数据一致，说明串口收发功能正常

**串口调试信息**：

系统会定期（约每秒1次）通过串口发送调试信息，包括：
- 接收帧数（RX Count）
- 错误计数（Error Count）
- USART3状态寄存器值（USART3_SR）
- 接收状态标志（RX_STA）

调试信息格式：
```
=== USART3 Debug ===
RX Count: 5
Error Count: 0
USART3_SR: 0x00C0
RX_STA: 0x0000
====================
```

```
#include "lcd_st7789.h"

int main(void)
{
    // 初始化LCD
    LCD_Init();
    
    // 清屏为白色背景
    LCD_Clear(COLOR_WHITE);
    
    // 在屏幕中央显示红色"hello"
    LCD_Show_String(130, 110, "hello", COLOR_RED, COLOR_WHITE);
    
    // 保持运行
    for(;;) {
        // 程序持续运行
    }
}
```

### 常用API函数

| 函数 | 说明 | 示例 |
|------|------|------|
| `LCD_Init()` | 初始化LCD | `LCD_Init();` |
| `LCD_Clear(color)` | 清屏 | `LCD_Clear(COLOR_WHITE);` |
| `LCD_Draw_Point(x, y, color)` | 绘制像素点 | `LCD_Draw_Point(100, 100, COLOR_RED);` |
| `LCD_Show_Char(x, y, ch, fg, bg)` | 显示字符 | `LCD_Show_Char(50, 50, 'A', COLOR_BLACK, COLOR_WHITE);` |
| `LCD_Show_String(x, y, str, fg, bg)` | 显示字符串 | `LCD_Show_String(130, 110, "hello", COLOR_RED, COLOR_WHITE);` |

### ⭐ 横屏显示配置（重要）

**ATM0240B32 模块横屏配置关键参数：**

1. **MADCTL（内存访问控制）寄存器**：
   - 横屏值：`0x60`（翻转180度横屏）
   - 地址范围必须匹配：
     - CASET（列地址）：0~319（宽度320）
     - RASET（行地址）：0~239（高度240）

2. **初始化代码示例**：
```c
// 横屏配置（在LCD_Init函数中）
uint8_t madctl_value = 0x60;  // 关键值！
LCD_Write_CmdData(0x36, &madctl_value, 1);  // MADCTL

// 地址范围设置
uint8_t col_addr[] = {0x00, 0x00, 0x01, 0x3F};  // 0~319
LCD_Write_CmdData(0x2A, col_addr, 4);  // CASET

uint8_t row_addr[] = {0x00, 0x00, 0x00, 0xEF};  // 0~239
LCD_Write_CmdData(0x2B, row_addr, 4);  // RASET
```

3. **调试经验**：
   - 如果屏幕显示为竖屏，MADCTL值可能在 `0x00`、`0x60`、`0xA0`、`0xC0` 之间
   - `0x60` 是ATM0240B32模块的正确横屏值（翻转180度）
   - 不同批次或不同尺寸的ST7789屏幕可能需要不同的MADCTL值
   - 如果文字方向不对，可尝试其他值（如 `0x00` 或 `0xA0`）

4. **常见错误**：
   - ❌ 使用 `0xC0` 会导致竖屏显示（某些屏幕）
   - ❌ 地址范围不匹配会导致花屏或显示错位
   - ✅ 必须先设置MADCTL，再设置地址范围

### 颜色定义（RGB565格式）

| 常量 | 值 | 颜色 |
|------|-----|------|
| COLOR_WHITE | 0xFFFF | 白色 |
| COLOR_BLACK | 0x0000 | 黑色 |
| COLOR_RED | 0xF800 | 红色 |
| COLOR_GREEN | 0x07E0 | 绿色 |
| COLOR_BLUE | 0x001F | 蓝色 |
| COLOR_YELLOW | 0xFFE0 | 黄色 |
| COLOR_CYAN | 0x07FF | 青色 |
| COLOR_MAGENTA | 0xF81F | 品红 |

### 显示效果

✅ LCD屏幕点亮  
✅ 白色背景清屏  
✅ 中央显示红色"hello"字样  
✅ PB1连接的LED常亮  

---

## 📊 完整硬件接线图

### **ATM0240B32 LCD模块完整接线：**

| LCD引脚 | STM32引脚 | 必需性 | 说明 |
|---------|-----------|--------|------|
| VCC     | 3.3V      | ✅ 必需 | 逻辑电源 |
| GND     | GND       | ✅ 必需 | 地线 |
| SCL     | PB3       | ✅ 必需 | SPI时钟 |
| SDI     | PB5       | ✅ 必需 | SPI数据输入 |
| CS      | PA12      | ✅ 必需 | 片选信号 |
| D/CX    | PB6       | ✅ 必需 | 数据/命令选择 |
| RST     | PA15      | ✅ 必需 | 复位信号 |
| **LEDA**| **3.3V**  | ✅ **必需** | **背光阳极** |
| **LEDK**| **PB7**   | ✅ **必需** | **背光阴极（软件控制）** |
| SDO     | PB4       | ❌ 可选 | SPI数据输出（只写模式可不接） |

⚠️ **重要提示**：
- **LEDA必须接3.3V**，否则背光无供电
- **LEDK必须接低电平**（GND或PB7输出LOW），否则背光不亮

---

## 🔥 烧录与测试

### **步骤1：确认接线**

```
检查以下连接：
✅ LEDA → 3.3V
✅ LEDK → PB7
✅ 其他SPI引脚正确连接
```

### **步骤2：烧录固件**

```powershell
cmake --build build/Debug --target flash_stlink_fast
```

### **步骤3：观察现象**

**预期效果**：
1. ✅ **PB1 LED常亮**（系统运行指示）
2. ✅ **LCD背光亮起**（PB7控制）
3. ✅ **上半屏红色**
4. ✅ **下半屏蓝色**
5. ✅ **中央黑色"hello"文字**

---

## 💡 LED指示功能

### 代码实现

```
// 使能GPIOB时钟
RCC_APB2ENR |= (1 << 3);

// 配置PB1为推挽输出，50MHz
GPIOB_CRL &= ~(0xF << 4);  // 清除配置
GPIOB_CRL |=  (0x3 << 4);  // 设置为输出

// 设置PB1为高电平（点亮LED）
GPIOB_ODR |= (1 << 1);
```

### 用途

- ✅ 确认板子正常运行
- ✅ 程序执行状态指示
- ✅ 调试辅助

---

## 🐛 故障排查

### ⭐ 显示方向错误（竖屏/横屏问题）

**症状**：
- 屏幕显示为竖屏，但代码期望横屏
- 文字方向旋转了90度
- 显示区域与预期不符

**解决方法**：

1. **调整MADCTL值**：
   ```c
   // 在LCD_Init()函数中修改MADCTL值
   uint8_t madctl_value = 0x60;  // ← 尝试不同的值
   
   // 常见横屏配置值（ATM0240B32模块）：
   // 0x60 - 翻转180度横屏 ✅ 推荐
   // 0x00 - 正常横屏
   // 0xA0 - 另一种横屏配置（可能带镜像）
   // 0xC0 - 标准横屏（某些屏幕）
   ```

2. **验证地址范围**：
   ```c
   // 横屏模式（320x240）的正确地址范围
   CASET（列地址）：0x0000 ~ 0x013F  (0~319，宽度320)
   RASET（行地址）：0x0000 ~ 0x00EF  (0~239，高度240)
   ```

3. **调试步骤**：
   - 如果当前是竖屏 → 尝试 `0x60` 或 `0xA0`
   - 如果文字上下颠倒 → 尝试 `0x00` 或翻转MADCTL
   - 如果文字左右镜像 → 尝试 `0xA0` 或调整MX/MY位

4. **经验总结**：
   - ATM0240B32模块的横屏正确值为 **`0x60`**
   - 不同批次或不同尺寸的ST7789屏幕可能需要不同的MADCTL值
   - 必须先设置MADCTL，再设置地址范围（CASET/RASET）

---

### **如果背光仍不亮**

#### **排查步骤：**

##### **1. 测量电压**
```
用万用表测量：
- LEDA引脚 → 应有3.3V
- LEDK引脚 → 应接近0V（PB7输出LOW时）
```

**如果LEDA无3.3V**：
- 检查3.3V供电
- 检查接线是否松动

**如果LEDK不是0V**：
- 检查PB7接线
- 确认代码中PB7配置正确

---

##### **2. 临时硬件测试**

将LEDK**直接连接到GND**（跳过PB7）：
```
STM32 GND ──→ LCD LEDK（临时）
```

**如果背光亮起**：
- 说明LCD模块正常
- 问题在PB7控制部分
- 检查PB7配置代码

**如果背光仍不亮**：
- LEDA可能未接3.3V
- LCD模块背光电路可能损坏
- 尝试更换LCD模块

---

##### **3. 检查PB7输出**

在代码中添加测试：

```c
// 测试PB7输出
GPIOB_ODR &= ~(1 << 7);  // PB7 LOW
delay_ms(1000);           // 延时1秒
GPIOB_ODR |= (1 << 7);   // PB7 HIGH
delay_ms(1000);           // 延时1秒
// 循环执行，观察背光是否闪烁
```

用万用表测量PB7引脚电压：
- 应为0V和3.3V交替变化

---

## 📝 版本历史

| 版本 | 日期 | 修改内容 |
|------|------|----------|
| v1.0 | 2026-10-08 | 初始版本，支持LCD显示和ST-Link烧录 |
| v1.1 | 2026-10-08 | **优化LCD驱动**：降低SPI速度、修正MADCTL、优化VCOM和伽马参数、添加LED指示 |
| v4.0 | 2026-10-09 | **PB1 LED 5秒循环闪烁 + PB7背光控制优化 + 详细故障排查指南** |
| v5.0 | 2026-10-10 | **横屏显示配置优化：MADCTL=0x60，黑底白字分页显示，添加横屏调试指南** |
| v6.0 | 2026-10-12 | **新增USART3串口通讯功能（PC10/PC11），第5页显示串口数据，支持5行循环显示** |
| **v6.1** | **2026-10-15** | **完善串口通讯功能：实现串口数据实时回显，优化第5页串口数据显示，添加串口调试信息输出** |
| **v6.2** | **2026-10-29** | **修复LCD与SWD/JTAG引脚冲突**：保留 SWJ_CFG=010 释放 PA15/PB3/PB4，同时保留 PA13/PA14 SWD；恢复稳定软件 SPI 时序。 |

---

**最后更新**: 2026-10-15  
**适用硬件**: STM32F103RCT6 + ST7789 LCD (ATM0240B32) + ST-Link V2  
**固件版本**: v6.1 (USART3 Serial Communication with Echo + Page 5 Display)  

**祝开发顺利！🎉**

### ST-Link 相关问题

#### 问题1：找不到ST-Link设备

**错误信息**：
```
ERROR: No ST-Link detected!
```

**解决方法**：
1. 检查USB连接是否牢固
2. 尝试更换USB端口
3. 检查设备管理器中ST-Link驱动状态
4. 使用Zadig工具安装WinUSB驱动：https://zadig.akeo.ie/

**检查驱动状态**：
```powershell
Get-PnpDevice | Where-Object {$_.FriendlyName -like "*ST-LINK*"} | Select-Object FriendlyName, Status
```

---

#### 问题2：烧录失败 - Target not found

**错误信息**：
```
ERROR: connect_failed
ERROR: send_recv failed
```

**解决方法**：
1. 检查SWCLK(PA14)和SWDIO(PA13)接线是否正确
2. 确保GND已连接
3. 确认STM32已上电（测量3.3V引脚电压）
4. 尝试降低SWD频率

**第二次烧录失败（`Failed to enter SWD mode`）的历史经验**：

- `PA15/PB3/PB4` 与 LCD 共用，必须用 `SWJ_CFG=010` 释放 PA15/PB3/PB4，同时保留 PA13/PA14 的 SWD。
- AFIO/SWJ 配置只由 `LCD_GPIO_Init()` 执行一次。`SystemInit()`、USART3、UART4 和主循环不得重复写入 `AFIO->MAPR`。
- 不要把 SWJ 配置写入 `SystemInit()` 或其他启动/周期函数中反复切换；当前已验证方案是在 `LCD_GPIO_Init()` 中执行一次必要配置。
- 不要同时禁用 JTAG 和 SWD；固件运行后会丢失 SWD 连接。
- 不要把无 `AFIO` 配置误认为是修复方案：只配置 GPIO 并不能释放 PA15/PB3/PB4 的默认调试复用功能。
- USART3 的 PB10/PB11 是默认复用引脚，初始化不得执行 `AFIO->MAPR &= ~AFIO_MAPR_USART3_REMAP` 或其他 AFIO 读写；重复写入 AFIO 会导致第二次 ST-Link 烧录异常。
- 硬件 NRST 未接时，ST-Link 会通过 AIRCR 软件复位；优先检查 PA13、PA14 和 GND，再检查固件的 SWJ 配置。
- RCC 72 MHz 不是此前问题的根因，除非示波器测得时钟异常，不应为了 SWD 或 LCD 临时降低系统时钟。

---

### 串口烧录相关问题

#### 问题3：No response from bootloader

**解决方法**：
1. 确认BOOT0=HIGH, BOOT1=LOW
2. 检查串口连接（TX↔RX, RX↔TX, GND↔GND）
3. 查看实际串口号：
   ```powershell
   [System.IO.Ports.SerialPort]::GetPortNames() | Sort-Object
   ```
4. 尝试降低波特率至57600

---

#### 问题4：程序烧录后不运行

**解决方法**：
1. 确保BOOT0=LOW, BOOT1=LOW
2. 断电后重新上电
3. 检查是否有看门狗或时钟配置问题

---

### LCD 显示相关问题

#### 问题5：屏幕黑屏无显示

**解决方法**：
1. 检查VCC和GND是否正确连接
2. 测量背光引脚是否有电压
3. 使用示波器检查SPI时钟和数据信号
4. 确认RST引脚有复位脉冲
5. **尝试修改MADCTL配置**（见下方优化说明）

> **已验证的 LCD 引脚冲突经验**
>
> 如果 LCD 不亮或完全无响应，同时目标板之前能正常多次 SWD 烧录，应优先检查 `LCD_GPIO_Init()` 中是否保留了下面的配置：
>
> ```c
> AFIO->MAPR &= ~AFIO_MAPR_SWJ_CFG;
> AFIO->MAPR |= AFIO_MAPR_SWJ_CFG_JTAGDISABLE;
> ```
>
> 仅把 `PA15/PB3/PB4` 配成推挽/浮空 GPIO 不足以释放它们的 JTAG复用；`PB4` 当前作为未使用的浮空输入时也必须保留 SWJ 配置。不要为了追求 LCD 正常而删除这段代码。
>
> USART3 使用 `PB10/PB11` 时，不应修改 `PA13/SWDIO`、`PA14/SWCLK`、重写 `SWJ_CFG` 或重复读写 `AFIO->MAPR`。JTAG/SWD复用只由 LCD初始化一次设置为 `SWJ_CFG=010`（保留 SW-DP，关闭 JTAG）；USART3 使用默认无重映射引脚，不执行任何 AFIO 重映射操作。重复 AFIO 写入曾导致二次 ST-Link 烧录失败。

---

#### 问题6：显示花屏或乱码

**解决方法**：
1. 降低SPI速度（已在v1.1优化中降至FPCLK/32）
2. 检查SDI、SCL连线是否牢固
3. 对比ST7789数据手册确认初始化序列

---

#### 问题7：颜色显示异常

**解决方法**：
```c
// 尝试修改MADCTL寄存器值
LCD_Write_Command(CMD_MADCTL);
LCD_Write_Data(0x00);  // 尝试：0x00, 0xC0, 0x60, 0xA0
```

---

### 编译相关问题

#### 问题8：arm-none-eabi-objcopy not found

**解决方法**：
```powershell
# 添加ARM GCC工具链到PATH
$env:Path = "D:\Tools\ARM_GCC\arm-gnu-toolchain-13.2.Rel1-mingw-w64-i686-arm-none-eabi\bin;" + $env:Path
```

---

#### 问题9：stm32flash not found

**解决方法**：
1. 下载stm32flash并添加到PATH
2. 或使用完整路径调用
3. 或使用PowerShell脚本自动检测

---

## 📊 性能指标

- **RAM占用**: 1976字节（4.02% / 48KB）
- **FLASH占用**: ~3KB（1.14% / 256KB）
- **SPI速度**: 软件 SPI，取决于编译优化和 GPIO 写入时序
- **清屏时间**: 取决于目标板和 LCD 模块，建议实测

## 🔋 RS485电池通讯

设备上电后，USART3 以 9600 bps 初始化；主循环依次轮询系统模拟量 `0x61`、告警/保护状态 `0x62`、充放电限制 `0x63` 和系统基本信息 `0x60`。一轮结束后等待 500 ms 再开始下一轮；实际周期还包括发送、响应和重试时间。通常每条请求等待响应最多 500 ms，超时重试，并根据协议尝试系统主机地址 `0x12` / `0x02`。`0x60` 长响应接收期间按字节活动延长等待，最长 5 秒；帧校验通过且长度等于 `33 + 16*N` 后，INFO 偏移 32 的 N 才作为电池数量保存。收到有效错误响应不会当作测量数据。

### USART波特率BRR编码经验

USART3出现持续发送 `00 00...`、波形乱码或USB-RS485助手无法识别帧头时，应同时检查波特率寄存器 `USART3->BRR`，不要只检查DE/RE和A/B接线。

STM32F103的BRR不是直接写入波特率，而是按下面的方式编码：

```text
USARTDIV = fCK / (16 * baud)
BRR.MANTISSA = floor(USARTDIV)
BRR.FRACTION  = round((USARTDIV - MANTISSA) * 16)
USART3->BRR = (MANTISSA << 4) | FRACTION
```

当前PCLK1为35MHz时：

```text
9600 bps：USARTDIV = 227.864583
BRR = 0xE3E
```

错误经验：曾经把`((fCK * 16) + baud/2) / baud`得到的值直接写入BRR。这个中间值已经是`USARTDIV*16`，再按BRR拆分时使用错误，实际会形成异常波特率，最终表现为全0、乱码或无法收到协议帧。当前代码必须使用上述MANTISSA/FRACTION编码；UART4也采用相同算法，35MHz PCLK1下115200对应BRR约为`0x130`。

修复后快速自检：

1. 确认串口助手设置为9600 bps、8N1。
2. 确认PB13发送时为高电平、接收时为低电平。
3. 用示波器抓PB10和PB13；PB10正常帧应从起始位0开始，数据内容不应为48字节连续`0x00`。
4. 正常请求帧应包含ASCII `0x7E`帧头，例如`7E 32 30 30 32...0D`，不是未初始化字节或全零帧。

PC5 每 250 ms 翻转一次，用于指示轮询功能已运行；这个心跳只表示轮询服务已启用，不代表电池一定在线。通讯状态和最新解析结果保存在 `g_battery_data`：

- `analog`：系统电压（mV）、带符号电流（mA，充电为正）、SOC/SOH、循环次数、电芯/MOS/BMS 温度（0.01°C）及模块地址。
- 系统电流按当前电池实测值转换：`0x61` 有符号原始值每单位为 10 mA，解析时乘以 10；首页和电池页共用该结果。此倍率与仓库协议文档中 `/1000 A` 的示例不一致，需结合实际电池返回帧与参考电流确认；充放电限制 `0x63` 的单位不受影响。
- `alarms`：分别保存两组告警和两组保护状态字节；`battery_rs485.h` 提供每一位的命名掩码，方便后续判断具体告警。
- `charge`：充放电电压限值（mV）、电流限值（0.1 A）和状态位。
- `available_mask`：对应字段有效位；协议中的 `0xFF` / `0xFFFF` 缺失值不会被当成有效测量值。
- `address`、`last_rtn`、`last_error` 和计数器：电池地址与通讯诊断信息。

LCD 共六页：第 2 页显示 DC/AC 逆变模块演示数据；第 3 页显示三路 MPPT 演示数据、累计发电量和告警占位；第 4 页显示 SOC、电压、充放电电流、平均/最小 SOH、电池状态、通讯情况及电池数量；第 5 页显示单芯、MOS、BMS 的最高/平均温度及四项告警/保护状态；第 6 页显示外部传感器演示值。电池告警位按 bit0 至 bit7 显示，例如 `1/4/6`；未知字段显示 `--`。电池页只刷新变化的行。当前超时后旧数值不会自动清除，应结合通讯情况判断数据是否仍然可靠。

首页以逆变器为中心，连接三路光伏 MPPT、电池和交流负载。三路 MPPT 电压、电流及输出箭头目前均为演示数据，正向电流使对应箭头从尾部到头部逐渐亮起，零值或负值保持灰色；数值超出显示宽度时显示“超限”。电池侧正电流表示从逆变器向电池充电，负电流表示电池向逆变器放电，零电流或电流未知时双向箭头保持灰色。交流负载尚无协议数据，显示 `--`，输入箭头保持灰色。右上角电量图标按有效 SOC 显示电量和百分比，充电时显示流光；SOC 未知时显示 `--`。首页和逆变页中的逆变器数值为同一组演示值，并非实测值。

MPPT 累计发电量为演示值；告警尚未接入协议，显示灰色 `--`。告警预留内容包括 PV 未接入、低压、高压、MPPT 缓起失败、电流硬件保护、PV 电流采样初始化异常、过载告警、过载保护和硬件保护锁死，具体编码及显示规则待协议确定。DC/AC 逆变模块尚无协议，演示数值显示为灰色，通讯状态未知；通讯状态为异常时显示红色“通讯异常”，正常时显示绿色“通讯正常”。后续协议接入时需更新 `inverter.communication`，并实现具体告警解析和动态数据刷新。

光伏、逆变器和环境演示值位于 `Modules/main.c` 的 `pv_routes`、`inverter` 和 `external_sensors`，后续可接入串口解析结果；目前数值在进入对应页面时绘制，接入动态数据时还需在数据变化后刷新相应显示区域。电池数据已使用 RS485 解析结果。

翻页时只清除原页面的文字、箭头、边框等显示区域；第 4、5 页之间保留共同的标题和边框，只清除数据行后重绘。软件 SPI 时序保持不变。

电池页中，SOC、SOH 和充电数据为绿色，电压和温度为青色，放电数据为橙色，电池数量为黄色；无数据时显示灰色。通讯及告警/保护状态正常为绿色、异常为红色，未知为灰色；电池空闲状态为青色。这些数据颜色不代表基于数值阈值的告警。
- **ST-Link烧录时间**: 2-5秒
- **串口烧录时间**: 10-20秒

---

## 📁 项目结构

```
stm32_1/
├── System/                 # STM32系统配置与启动文件
│   ├── core_cm3.h          # Cortex-M3 CMSIS核心定义
│   ├── stm32f10x.h         # STM32F103设备定义
│   ├── system_stm32f10x.*  # 时钟与系统初始化
│   ├── stm32f10x_it.*      # 异常与中断服务入口
│   ├── startup_stm32f103xx.S  # 启动与向量表
│   ├── syscall.c           # Newlib系统调用
│   ├── sysmem.c            # 堆内存管理
│   └── stm32f103xc_flash.ld # 链接脚本
├── Modules/                # 应用逻辑与功能模块
│   ├── main.c              # 主程序
│   ├── lcd_st7789.c/h      # ST7789显示驱动
│   ├── battery_rs485.c/h   # 电池RS485通讯模块
│   └── chinese_font.c/h    # 中文字库
├── cmake/                  # CMake配置文件
│   ├── target.cmake       # 目标芯片定义
│   ├── flags.cmake        # 编译标志
│   ├── files.cmake        # 源文件列表（已更新）
│   ├── components.cmake   # 组件配置
│   └── gcc-arm-none-eabi.cmake  # 工具链配置
├── build/                  # 构建输出目录
├── CMakeLists.txt         # 主CMake配置
├── CMakePresets.json      # CMake预设配置
├── flash_stlink.ps1       # ST-Link烧录脚本 ⭐
├── flash_stm32.ps1        # 串口烧录脚本
├── install_stlink_tools.ps1  # 工具安装脚本
└── README.md              # 本文件 📖
```

---

## 🔗 相关资源

- [ST7789数据手册](https://www.displayfuture.com/Display/datasheet/controller/ST7789.pdf)
- [STM32F103参考手册](https://www.st.com/resource/en/reference_manual/dm00031020.pdf)
- [stlink-tools](https://github.com/stlink-org/stlink)
- [ARM GCC工具链](https://developer.arm.com/tools-and-software/open-source-software/developer-tools/gnu-toolchain)
- [Zadig驱动工具](https://zadig.akeo.ie/)

---

## 📝 版本历史

| 版本 | 日期 | 修改内容 |
|------|------|----------|
| v1.0 | 2026-10-08 | 初始版本，支持LCD显示和ST-Link烧录 |
| v1.1 | 2026-10-08 | **优化LCD驱动**：降低SPI速度、修正MADCTL、优化VCOM和伽马参数、添加LED指示 |
| v4.0 | 2026-10-09 | **PB1 LED 5秒循环闪烁 + PB7背光控制优化 + 详细故障排查指南** |
| v5.0 | 2026-10-10 | **横屏显示配置优化：MADCTL=0x60，黑底白字分页显示，添加横屏调试指南** |
| v6.0 | 2026-10-12 | **新增USART3串口通讯功能（PC10/PC11），第5页显示串口数据，支持5行循环显示** |
| **v6.1** | **2026-10-15** | **完善串口通讯功能：实现串口数据实时回显，优化第5页串口数据显示，添加串口调试信息输出** |

---

**最后更新**: 2026-10-15  
**适用硬件**: STM32F103RCT6 + ST7789 LCD (ATM0240B32) + ST-Link V2  
**固件版本**: v6.1 (USART3 Serial Communication with Echo + Page 5 Display)  

**祝开发顺利！🎉**
