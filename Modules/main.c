/**
 * Off-grid energy monitor UI for a 320x240 ST7789 LCD.
 */

#include "stm32f10x.h"
#include "lcd_st7789.h"
#include "battery_rs485.h"
#include "battery_pages.h"
#include "wifi_uart4.h"
#include "startup_logo.h"

#include <stdio.h>
#include <stdint.h>

#define UI_PAGE_OVERVIEW  0u
#define UI_PAGE_PV        1u
#define UI_PAGE_BATTERY   2u
#define UI_PAGE_BATTERY_DETAIL 3u
#define UI_PAGE_SENSOR    4u
#define UI_PAGE_COUNT     5u

typedef struct {
    uint32_t voltage_mv;
    int32_t current_ma;
} PvRouteData;

typedef struct {
    int16_t temperature_tenth_c;
    uint16_t humidity_tenth_percent;
    uint16_t wind_tenth_mps;
    uint8_t rain_detected;
} ExternalSensorData;

typedef struct {
    uint32_t available_mask;
    uint8_t address_valid;
    uint16_t system_voltage_mv;
    int32_t system_current_ma;
    uint8_t system_soc_percent;
    uint8_t average_soh_percent;
    int32_t average_cell_temperature_centi_c;
    int32_t charge_current_ma;
    int32_t discharge_current_ma;
    uint8_t alarm_status_1;
    uint8_t alarm_status_2;
    uint8_t protection_status_1;
    uint8_t protection_status_2;
    uint8_t charge_status;
    BatteryError last_error;
} BatteryUiSnapshot;

/* Replace these demonstration values when the three PV channels and sensors are wired. */
static PvRouteData pv_routes[3] = {
    {22500u, 5300},
    {33800u, 6100},
    {45200u, 4800}
};

static ExternalSensorData external_sensors = {
    285,
    452,
    32,
    0
};

static void delay_ms(volatile uint32_t ms)
{
    while(ms--) {
        for(volatile uint32_t i = 0u; i < 500u; i++) {
            __asm__("nop");
        }
    }
}

static void delay_key_debounce(void)
{
    delay_ms(1u);
}

static void draw_border(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color)
{
    LCD_Fill(x1, y1, x2, y1 + 1u, color);
    LCD_Fill(x1, y2 - 1u, x2, y2, color);
    LCD_Fill(x1, y1, x1 + 1u, y2, color);
    LCD_Fill(x2 - 1u, y1, x2, y2, color);
}

static void draw_arrow(uint16_t x, uint16_t y, uint16_t length, uint16_t color)
{
    LCD_Fill(x, y - 1u, x + length, y + 1u, color);
    LCD_Fill(x + length - 5u, y - 5u, x + length, y - 1u, color);
    LCD_Fill(x + length - 5u, y + 1u, x + length, y + 5u, color);
}

static void format_value(char *buffer, uint16_t size, int32_t value_tenths, const char *unit)
{
    char sign = (value_tenths < 0) ? '-' : ' ';
    uint32_t value = (value_tenths < 0) ? (uint32_t)(-value_tenths) : (uint32_t)value_tenths;

    (void)snprintf(buffer, size, "%c%u.%u%s",
                   sign, (unsigned int)(value / 10u),
                   (unsigned int)(value % 10u), unit);
}

static uint8_t battery_data_available(void)
{
    return g_battery_data.address_valid &&
           (g_battery_data.analog.available_mask &
            ((uint32_t)1u << BATTERY_ANALOG_TOTAL_VOLTAGE));
}

static uint16_t battery_soc(void)
{
    if((g_battery_data.analog.available_mask &
        ((uint32_t)1u << BATTERY_ANALOG_SOC)) == 0u) {
        return 0u;
    }
    return g_battery_data.analog.system_soc_percent;
}

static BatteryUiSnapshot battery_ui_snapshot(void)
{
    int32_t charge_current_ma = g_battery_data.analog.system_current_ma > 0
                                    ? g_battery_data.analog.system_current_ma : 0;
    int32_t discharge_current_ma = g_battery_data.analog.system_current_ma < 0
                                       ? -g_battery_data.analog.system_current_ma : 0;
    BatteryUiSnapshot snapshot = {
        g_battery_data.analog.available_mask,
        g_battery_data.address_valid,
        g_battery_data.analog.system_voltage_mv,
        g_battery_data.analog.system_current_ma,
        g_battery_data.analog.system_soc_percent,
        g_battery_data.analog.average_soh_percent,
        g_battery_data.analog.average_cell_temperature_centi_c,
        charge_current_ma,
        discharge_current_ma,
        g_battery_data.alarms.alarm_status_1,
        g_battery_data.alarms.alarm_status_2,
        g_battery_data.alarms.protection_status_1,
        g_battery_data.alarms.protection_status_2,
        g_battery_data.charge.charge_discharge_status,
        g_battery_data.last_error
    };
    return snapshot;
}

static uint8_t battery_ui_changed(const BatteryUiSnapshot *previous)
{
    BatteryUiSnapshot current = battery_ui_snapshot();
    return previous->available_mask != current.available_mask ||
           previous->address_valid != current.address_valid ||
           previous->system_voltage_mv != current.system_voltage_mv ||
           previous->system_current_ma != current.system_current_ma ||
           previous->system_soc_percent != current.system_soc_percent ||
           previous->average_soh_percent != current.average_soh_percent ||
           previous->average_cell_temperature_centi_c != current.average_cell_temperature_centi_c ||
           previous->charge_current_ma != current.charge_current_ma ||
           previous->discharge_current_ma != current.discharge_current_ma ||
           previous->alarm_status_1 != current.alarm_status_1 ||
           previous->alarm_status_2 != current.alarm_status_2 ||
           previous->protection_status_1 != current.protection_status_1 ||
           previous->protection_status_2 != current.protection_status_2 ||
           previous->charge_status != current.charge_status ||
           previous->last_error != current.last_error;
}

static void display_battery_header(void)
{
    char text[24];
    uint16_t soc = battery_soc();

    LCD_Fill(0u, 0u, 319u, 27u, COLOR_BLACK);
    LCD_Show_String_With_Chinese(6u, 6u, "离网系统", COLOR_WHITE, COLOR_BLACK);

    if(battery_data_available()) {
        (void)snprintf(text, sizeof(text), "电池 %u%%", soc);
        LCD_Show_String_With_Chinese(214u, 6u, text, COLOR_GREEN, COLOR_BLACK);
    } else {
        LCD_Show_String_With_Chinese(214u, 6u, "电池 --", COLOR_YELLOW, COLOR_BLACK);
    }

    LCD_Fill(0u, 28u, 319u, 29u, COLOR_GRAY);
}

static void display_overview(void)
{
    char text[24];

    LCD_Fill_Fast(COLOR_BLACK);
    display_battery_header();

    LCD_Show_String_With_Chinese(12u, 38u, "光伏输入", COLOR_YELLOW, COLOR_BLACK);
    draw_border(6u, 34u, 112u, 168u, COLOR_GRAY);

    for(uint8_t route = 0u; route < 3u; route++) {
        uint16_t y = 58u + route * 34u;

        const char *route_name[3] = {"光伏一", "光伏二", "光伏三"};
        (void)snprintf(text, sizeof(text), "%s", route_name[route]);
        LCD_Show_String_With_Chinese(12u, y, text, COLOR_WHITE, COLOR_BLACK);

        (void)snprintf(text, sizeof(text), "%u.%uV",
                       (unsigned int)(pv_routes[route].voltage_mv / 1000u),
                       (unsigned int)((pv_routes[route].voltage_mv / 100u) % 10u));
        LCD_Show_String_With_Chinese(72u, y, text, COLOR_CYAN, COLOR_BLACK);

        int32_t current_tenths = pv_routes[route].current_ma / 100u;
        format_value(text, sizeof(text), current_tenths, "A");
        LCD_Show_String_With_Chinese(72u, y + 16u, text, COLOR_WHITE, COLOR_BLACK);

        uint32_t power_w = (uint32_t)pv_routes[route].voltage_mv *
                           (pv_routes[route].current_ma > 0 ? (uint32_t)pv_routes[route].current_ma : 0u) /
                           1000000u;
        (void)snprintf(text, sizeof(text), "%luW", (unsigned long)power_w);
        LCD_Show_String_With_Chinese(12u, y + 16u, text, COLOR_GREEN, COLOR_BLACK);
    }

    draw_arrow(114u, 102u, 34u, COLOR_GREEN);

    draw_border(148u, 34u, 224u, 168u, COLOR_GRAY);
    LCD_Show_String_With_Chinese(160u, 38u, "电池", COLOR_YELLOW, COLOR_BLACK);

    if(battery_data_available()) {
        uint16_t soc = battery_soc();
        uint16_t voltage_tenths = (uint16_t)(g_battery_data.analog.system_voltage_mv / 100u);
        int32_t current_tenths = g_battery_data.analog.system_current_ma / 100;

        (void)snprintf(text, sizeof(text), "%u%%", soc);
        LCD_Show_String_With_Chinese(168u, 62u, text, COLOR_GREEN, COLOR_BLACK);
        format_value(text, sizeof(text), (int32_t)voltage_tenths, "V");
        LCD_Show_String_With_Chinese(158u, 86u, text, COLOR_WHITE, COLOR_BLACK);
        format_value(text, sizeof(text), current_tenths, "A");
        LCD_Show_String_With_Chinese(158u, 106u, text,
                                    current_tenths >= 0 ? COLOR_GREEN : COLOR_ORANGE,
                                    COLOR_BLACK);

        if((g_battery_data.analog.available_mask &
            ((uint32_t)1u << BATTERY_ANALOG_AVERAGE_CELL_TEMPERATURE)) != 0u) {
            int32_t temperature_tenths =
                g_battery_data.analog.average_cell_temperature_centi_c / 10u;
            format_value(text, sizeof(text), temperature_tenths, "C");
            LCD_Show_String_With_Chinese(158u, 126u, text, COLOR_CYAN, COLOR_BLACK);
        } else {
            LCD_Show_String_With_Chinese(158u, 126u, "-- C", COLOR_GRAY, COLOR_BLACK);
        }
    } else {
        LCD_Show_String_With_Chinese(168u, 62u, "--%", COLOR_YELLOW, COLOR_BLACK);
        LCD_Show_String_With_Chinese(158u, 86u, "--V", COLOR_GRAY, COLOR_BLACK);
        LCD_Show_String_With_Chinese(158u, 106u, "--A", COLOR_GRAY, COLOR_BLACK);
    }

    draw_arrow(226u, 102u, 34u, COLOR_CYAN);

    draw_border(260u, 34u, 314u, 168u, COLOR_GRAY);
    LCD_Show_String_With_Chinese(268u, 38u, "交流", COLOR_YELLOW, COLOR_BLACK);
    LCD_Show_String_With_Chinese(266u, 62u, "620W", COLOR_WHITE, COLOR_BLACK);
    LCD_Show_String_With_Chinese(266u, 86u, "220V", COLOR_CYAN, COLOR_BLACK);
    LCD_Show_String_With_Chinese(266u, 106u, "50Hz", COLOR_WHITE, COLOR_BLACK);
    LCD_Show_String_With_Chinese(266u, 126u, "正常", COLOR_GREEN, COLOR_BLACK);

    LCD_Fill(0u, 176u, 319u, 177u, COLOR_GRAY);
    LCD_Show_String_With_Chinese(8u, 184u, "环境", COLOR_YELLOW, COLOR_BLACK);

    format_value(text, sizeof(text), external_sensors.temperature_tenth_c, "C");
    LCD_Show_String_With_Chinese(56u, 184u, text, COLOR_CYAN, COLOR_BLACK);

    format_value(text, sizeof(text), (int32_t)external_sensors.humidity_tenth_percent, "%");
    LCD_Show_String_With_Chinese(112u, 184u, text, COLOR_WHITE, COLOR_BLACK);

    format_value(text, sizeof(text), (int32_t)external_sensors.wind_tenth_mps, "m/s");
    LCD_Show_String_With_Chinese(176u, 184u, text, COLOR_WHITE, COLOR_BLACK);

    LCD_Show_String_With_Chinese(248u, 184u,
                                 external_sensors.rain_detected ? "检测" : "干燥",
                                 external_sensors.rain_detected ? COLOR_BLUE : COLOR_GREEN,
                                 COLOR_BLACK);

    LCD_Fill(0u, 207u, 319u, 208u, COLOR_GRAY);
    LCD_Show_String_With_Chinese(8u, 216u, "系统正常", COLOR_GREEN, COLOR_BLACK);
    LCD_Show_String_With_Chinese(242u, 216u, "1/5", COLOR_CYAN, COLOR_BLACK);
}

static void display_pv_page(void)
{
    char text[32];

    LCD_Fill_Fast(COLOR_BLACK);
    LCD_Show_String_With_Chinese(8u, 8u, "光伏通道", COLOR_YELLOW, COLOR_BLACK);
    LCD_Show_String_With_Chinese(278u, 8u, "2/5", COLOR_CYAN, COLOR_BLACK);

    for(uint8_t route = 0u; route < 3u; route++) {
        uint16_t y = 44u + route * 58u;
        draw_border(8u, y - 8u, 312u, y + 44u, COLOR_GRAY);

        const char *route_name[3] = {"光伏一", "光伏二", "光伏三"};
        (void)snprintf(text, sizeof(text), "%s", route_name[route]);
        LCD_Show_String_With_Chinese(18u, y, text, COLOR_WHITE, COLOR_BLACK);

        (void)snprintf(text, sizeof(text), "电压 %u.%uV",
                       (unsigned int)(pv_routes[route].voltage_mv / 1000u),
                       (unsigned int)((pv_routes[route].voltage_mv / 100u) % 10u));
        LCD_Show_String_With_Chinese(78u, y, text, COLOR_CYAN, COLOR_BLACK);

        format_value(text, sizeof(text), pv_routes[route].current_ma / 100u, "A");
        LCD_Show_String_With_Chinese(78u, y + 20u, text, COLOR_WHITE, COLOR_BLACK);

        uint32_t power_w = (uint32_t)pv_routes[route].voltage_mv *
                           (pv_routes[route].current_ma > 0 ? (uint32_t)pv_routes[route].current_ma : 0u) /
                           1000000u;
        (void)snprintf(text, sizeof(text), "功率 %luW", (unsigned long)power_w);
        LCD_Show_String_With_Chinese(190u, y + 20u, text, COLOR_GREEN, COLOR_BLACK);
    }

    uint32_t total_w = 0u;
    for(uint8_t route = 0u; route < 3u; route++) {
        total_w += (uint32_t)pv_routes[route].voltage_mv *
                   (pv_routes[route].current_ma > 0 ? (uint32_t)pv_routes[route].current_ma : 0u) /
                   1000000u;
    }
    (void)snprintf(text, sizeof(text), "光伏总功率  %luW", (unsigned long)total_w);
    LCD_Show_String_With_Chinese(18u, 218u, text, COLOR_GREEN, COLOR_BLACK);
}

static void display_sensor_page(void)
{
    char text[32];

    LCD_Fill_Fast(COLOR_BLACK);
    LCD_Show_String_With_Chinese(8u, 8u, "外部传感器", COLOR_YELLOW, COLOR_BLACK);
    LCD_Show_String_With_Chinese(278u, 8u, "5/5", COLOR_CYAN, COLOR_BLACK);

    draw_border(8u, 38u, 312u, 198u, COLOR_GRAY);

    LCD_Show_String_With_Chinese(18u, 52u, "温度", COLOR_WHITE, COLOR_BLACK);
    format_value(text, sizeof(text), external_sensors.temperature_tenth_c, "C");
    LCD_Show_String_With_Chinese(180u, 52u, text, COLOR_CYAN, COLOR_BLACK);

    LCD_Show_String_With_Chinese(18u, 82u, "湿度", COLOR_WHITE, COLOR_BLACK);
    format_value(text, sizeof(text), (int32_t)external_sensors.humidity_tenth_percent, "%");
    LCD_Show_String_With_Chinese(180u, 82u, text, COLOR_WHITE, COLOR_BLACK);

    LCD_Show_String_With_Chinese(18u, 112u, "风速", COLOR_WHITE, COLOR_BLACK);
    format_value(text, sizeof(text), (int32_t)external_sensors.wind_tenth_mps, "m/s");
    LCD_Show_String_With_Chinese(180u, 112u, text, COLOR_WHITE, COLOR_BLACK);

    LCD_Show_String_With_Chinese(18u, 142u, "雨水", COLOR_WHITE, COLOR_BLACK);
    LCD_Show_String_With_Chinese(180u, 142u,
                                 external_sensors.rain_detected ? "检测" : "干燥",
                                 external_sensors.rain_detected ? COLOR_BLUE : COLOR_GREEN,
                                 COLOR_BLACK);

    LCD_Show_String_With_Chinese(18u, 216u, "演示数据", COLOR_GRAY, COLOR_BLACK);
}

static void display_page(uint8_t page)
{
    static uint8_t previous_page = UI_PAGE_COUNT;
    uint8_t reuse_battery_layout = previous_page == UI_PAGE_BATTERY ||
                                   previous_page == UI_PAGE_BATTERY_DETAIL;
    switch(page) {
        case UI_PAGE_PV:
            display_pv_page();
            break;
        case UI_PAGE_BATTERY:
            display_battery_page(BATTERY_PAGE_SUMMARY, reuse_battery_layout);
            break;
        case UI_PAGE_BATTERY_DETAIL:
            display_battery_page(BATTERY_PAGE_DETAIL, reuse_battery_layout);
            break;
        case UI_PAGE_SENSOR:
            display_sensor_page();
            break;
        default:
            display_overview();
            break;
    }
    previous_page = page;
}

int main(void)
{
    LCD_Init();
    KEY_GPIO_Init();
    Battery_RS485_Init();
    WiFi_UART4_Init();

    uint8_t current_page = UI_PAGE_OVERVIEW;
    uint8_t showing_startup = 1u;
    uint8_t key_esc_released = 1u;
    uint8_t key_up_released = 1u;
    uint8_t key_down_released = 1u;
    BatteryUiSnapshot battery_snapshot = battery_ui_snapshot();

    LCD_Fill_Fast(COLOR_WHITE);
    LCD_Draw_4Bit_Bitmap((LCD_WIDTH - STARTUP_LOGO_WIDTH) / 2u,
                         (LCD_HEIGHT - STARTUP_LOGO_HEIGHT) / 2u,
                         STARTUP_LOGO_WIDTH, STARTUP_LOGO_HEIGHT,
                         startup_logo, startup_logo_palette);

    while(1) {
        Battery_RS485_PollService();

        if(KEY_ESC_PRESSED()) {
            if(key_esc_released) {
                delay_key_debounce();
                if(KEY_ESC_PRESSED()) {
                    key_esc_released = 0u;
                    if(showing_startup || current_page != UI_PAGE_OVERVIEW) {
                        showing_startup = 0u;
                        current_page = UI_PAGE_OVERVIEW;
                        display_page(current_page);
                    }
                    key_up_released = !KEY_PAGEUP_PRESSED();
                    key_down_released = !KEY_PAGEDOWN_PRESSED();
                }
            }
        } else {
            key_esc_released = 1u;
        }

        if(showing_startup) {
            delay_ms(10u);
            continue;
        }

        if(KEY_PAGEDOWN_PRESSED()) {
            if(key_down_released) {
                delay_key_debounce();
                if(KEY_PAGEDOWN_PRESSED()) {
                    key_down_released = 0u;
                    current_page++;
                    if(current_page >= UI_PAGE_COUNT) {
                        current_page = UI_PAGE_OVERVIEW;
                    }
                    display_page(current_page);
                }
            }
        } else {
            key_down_released = 1u;
        }

        if(KEY_PAGEUP_PRESSED()) {
            if(key_up_released) {
                delay_key_debounce();
                if(KEY_PAGEUP_PRESSED()) {
                    key_up_released = 0u;
                    if(current_page == UI_PAGE_OVERVIEW) {
                        current_page = UI_PAGE_COUNT - 1u;
                    } else {
                        current_page--;
                    }
                    display_page(current_page);
                }
            }
        } else {
            key_up_released = 1u;
        }

        if(battery_ui_changed(&battery_snapshot)) {
            if(current_page == UI_PAGE_OVERVIEW) {
                display_page(current_page);
            }
            battery_snapshot = battery_ui_snapshot();
        }
        if(current_page == UI_PAGE_BATTERY || current_page == UI_PAGE_BATTERY_DETAIL) {
            battery_page_update(current_page == UI_PAGE_BATTERY ? BATTERY_PAGE_SUMMARY : BATTERY_PAGE_DETAIL);
        }

        delay_ms(10u);
    }
}
