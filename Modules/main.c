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
#include <string.h>

#define UI_PAGE_OVERVIEW  0u
#define UI_PAGE_INVERTER  1u
#define UI_PAGE_PV        2u
#define UI_PAGE_BATTERY   3u
#define UI_PAGE_BATTERY_DETAIL 4u
#define UI_PAGE_SENSOR    5u
#define UI_PAGE_COUNT     6u

typedef struct {
    uint32_t voltage_mv;
    int32_t current_ma;
    uint32_t energy_tenth_kwh;
} PvRouteData;

typedef enum {
    INVERTER_COMM_UNKNOWN,
    INVERTER_COMM_NORMAL,
    INVERTER_COMM_ERROR
} InverterCommunication;

typedef struct {
    uint16_t ac_voltage_tenth_v;
    uint16_t ac_frequency_tenth_hz;
    uint16_t ac_current_tenth_a;
    uint32_t ac_power_w;
    uint16_t charger_voltage_tenth_v;
    uint32_t charger_power_w;
    InverterCommunication communication;
} InverterData;

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

/* Replace these demonstration values when the corresponding protocols are connected. */
static PvRouteData pv_routes[3] = {
    {0u, 0, 0u},
    {33800u, 6100, 1234u},
    {45200u, 4800, 2567u}
};

static InverterData inverter = {
    2200u, 500u, 28u, 620u, 2300u, 500u, INVERTER_COMM_UNKNOWN
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

static void draw_arrow(uint16_t x, uint16_t y, uint16_t length,
                       uint8_t right, uint16_t color)
{
    uint16_t tip = right ? x + length : x;
    LCD_Fill(x, y, x + length, y + 1u, color);
    for(uint16_t i = 1u; i <= 4u; i++) {
        uint16_t arrow_x = right ? tip - i : tip + i;
        LCD_Fill(arrow_x, y - i, arrow_x, y + 1u + i, color);
    }
}

static void draw_flow_arrow(uint16_t x, uint16_t y, uint16_t length,
                            uint8_t right, uint8_t active, uint8_t phase,
                            uint16_t color)
{
    draw_arrow(x, y, length, right, COLOR_GRAY);
    if(active) {
        uint16_t progress = (uint16_t)(((uint32_t)(length + 1u) * (phase + 1u)) / 8u);
        if(right) {
            LCD_Fill(x, y, x + progress - 1u, y + 1u, color);
        } else {
            LCD_Fill(x + length + 1u - progress, y, x + length, y + 1u, color);
        }
        if(phase == 7u) {
            uint16_t tip = right ? x + length : x;
            for(uint16_t i = 1u; i <= 4u; i++) {
                uint16_t arrow_x = right ? tip - i : tip + i;
                LCD_Fill(arrow_x, y - i, arrow_x, y + 1u + i, color);
            }
        }
    }
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

static uint8_t battery_field_available(BatteryAnalogField field)
{
    return g_battery_data.address_valid &&
           (g_battery_data.analog.available_mask & ((uint32_t)1u << field)) != 0u;
}

static void display_overview_animation(uint8_t force)
{
    static uint8_t previous_phase;
    static uint8_t previous_flow;
    static uint8_t previous_soc;
    static uint8_t previous_soc_available;
    static uint8_t previous_charging;
    uint8_t phase = (uint8_t)((Battery_RS485_GetTickMs() / 200u) % 8u);
    uint8_t flow = 0u;
    uint8_t soc_available = battery_field_available(BATTERY_ANALOG_SOC);
    uint8_t soc = soc_available ? g_battery_data.analog.system_soc_percent : 0u;
    uint8_t current_available = battery_field_available(BATTERY_ANALOG_TOTAL_CURRENT);
    uint8_t charging = current_available && g_battery_data.analog.system_current_ma > 0;

    for(uint8_t route = 0u; route < 3u; route++) {
        if(pv_routes[route].current_ma > 0) flow |= (uint8_t)(1u << route);
    }
    if(charging) flow |= 0x08u;
    if(current_available && g_battery_data.analog.system_current_ma < 0) flow |= 0x10u;

    for(uint8_t route = 0u; route < 3u; route++) {
        uint8_t mask = (uint8_t)(1u << route);
        if(force || ((flow ^ previous_flow) & mask) || ((flow & mask) && phase != previous_phase)) {
            draw_flow_arrow(107u, 55u + route * 46u, 32u, 1u,
                            (flow & mask) != 0u, phase, COLOR_GREEN);
        }
    }
    if(force || ((flow ^ previous_flow) & 0x08u) || ((flow & 0x08u) && phase != previous_phase))
        draw_flow_arrow(208u, 63u, 35u, 1u, (flow & 0x08u) != 0u, phase, COLOR_GREEN);
    if(force || ((flow ^ previous_flow) & 0x10u) || ((flow & 0x10u) && phase != previous_phase))
        draw_flow_arrow(208u, 95u, 35u, 0u, (flow & 0x10u) != 0u, phase, COLOR_ORANGE);
    if(force) draw_arrow(208u, 147u, 35u, 1u, COLOR_GRAY);

    if(force || soc != previous_soc || soc_available != previous_soc_available) {
        char text[8];
        if(soc_available) {
            (void)snprintf(text, sizeof(text), "%u%%", (unsigned int)soc);
        } else {
            (void)snprintf(text, sizeof(text), "--");
        }
        LCD_Fill(249u, 6u, 280u, 21u, COLOR_BLACK);
        LCD_Show_String_With_Chinese(249u, 6u, text,
                                     soc_available ? COLOR_WHITE : COLOR_GRAY, COLOR_BLACK);
        draw_border(286u, 6u, 313u, 21u, soc_available ? COLOR_WHITE : COLOR_GRAY);
        LCD_Fill(314u, 11u, 316u, 16u, soc_available ? COLOR_WHITE : COLOR_GRAY);
    }
    if(force || soc != previous_soc || soc_available != previous_soc_available ||
       charging != previous_charging || (charging && phase != previous_phase)) {
        uint8_t level = soc > 100u ? 100u : soc;
        uint16_t fill_width = (uint16_t)((uint16_t)level * 22u / 100u);
        LCD_Fill(289u, 9u, 310u, 18u, COLOR_BLACK);
        if(soc_available && fill_width != 0u) {
            LCD_Fill(289u, 9u, 288u + fill_width, 18u,
                     level <= 20u ? COLOR_RED : COLOR_GREEN);
        }
        if(charging) {
            uint16_t x = 289u + (uint16_t)phase * 3u;
            if(x <= 310u) LCD_Fill(x, 9u, x + 1u <= 310u ? x + 1u : 310u, 18u, COLOR_CYAN);
        }
    }
    previous_phase = phase;
    previous_flow = flow;
    previous_soc = soc;
    previous_soc_available = soc_available;
    previous_charging = charging;
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
    LCD_Show_String_With_Chinese(6u, 6u, "离网系统", COLOR_WHITE, COLOR_BLACK);
    LCD_Fill(0u, 28u, 319u, 29u, COLOR_GRAY);
}

static void display_compact_value(uint16_t x, uint16_t y, int32_t tenths,
                                  const char *unit, uint16_t color, uint8_t max_chars)
{
    char text[24];
    const char *value;
    format_value(text, sizeof(text), tenths, unit);
    value = text[0] == ' ' ? text + 1 : text;
    LCD_Show_String_With_Chinese(x, y, strlen(value) <= max_chars ? value : "超限", color, COLOR_BLACK);
}

static void display_compact_temperature(uint16_t x, uint16_t y, int32_t tenths,
                                        uint8_t max_digits)
{
    char number[24], text[28];
    const char *value;
    format_value(number, sizeof(number), tenths, "");
    value = number[0] == ' ' ? number + 1 : number;
    if(strlen(value) <= max_digits) {
        (void)snprintf(text, sizeof(text), "%s℃", value);
    } else {
        (void)snprintf(text, sizeof(text), "超限℃");
    }
    LCD_Show_String_With_Chinese(x, y, text, COLOR_CYAN, COLOR_BLACK);
}

static void display_overview_battery_data(void)
{
    char text[24];

    display_overview_animation(0u);

    if(battery_data_available()) {
        uint16_t voltage_tenths = (uint16_t)(g_battery_data.analog.system_voltage_mv / 100u);
        format_value(text, sizeof(text), (int32_t)voltage_tenths, "V");
        LCD_Fill(248u, 56u, 311u, 71u, COLOR_BLACK);
        LCD_Show_String_With_Chinese(248u, 56u, text, COLOR_WHITE, COLOR_BLACK);
    } else {
        LCD_Fill(248u, 56u, 311u, 71u, COLOR_BLACK);
        LCD_Show_String_With_Chinese(248u, 56u, "--V", COLOR_GRAY, COLOR_BLACK);
    }
    if(battery_field_available(BATTERY_ANALOG_TOTAL_CURRENT)) {
        int32_t current_tenths = g_battery_data.analog.system_current_ma / 100;
        format_value(text, sizeof(text), current_tenths, "A");
        LCD_Fill(248u, 75u, 311u, 90u, COLOR_BLACK);
        LCD_Show_String_With_Chinese(248u, 75u, text,
                                    current_tenths >= 0 ? COLOR_GREEN : COLOR_ORANGE,
                                    COLOR_BLACK);
    } else {
        LCD_Fill(248u, 75u, 311u, 90u, COLOR_BLACK);
        LCD_Show_String_With_Chinese(248u, 75u, "--A", COLOR_GRAY, COLOR_BLACK);
    }
    LCD_Fill(248u, 94u, 311u, 109u, COLOR_BLACK);
    if(battery_field_available(BATTERY_ANALOG_AVERAGE_CELL_TEMPERATURE)) {
        int32_t temperature_tenths = g_battery_data.analog.average_cell_temperature_centi_c / 10;
        display_compact_temperature(248u, 94u, temperature_tenths, 6u);
    } else {
        LCD_Show_String_With_Chinese(248u, 94u, "--℃", COLOR_GRAY, COLOR_BLACK);
    }
}

static void display_overview(void)
{
    char text[24];

    display_battery_header();

    for(uint8_t route = 0u; route < 3u; route++) {
        uint16_t y = 34u + route * 46u;
        draw_border(4u, y, 106u, y + 42u, COLOR_WHITE);
        static const char *const route_names[] = {"PV1", "PV2", "PV3"};
        LCD_Show_String_With_Chinese(8u, y + 4u, route_names[route], COLOR_YELLOW, COLOR_BLACK);
        display_compact_value(8u, y + 22u, (int32_t)(pv_routes[route].voltage_mv / 100u),
                              "V", COLOR_CYAN, 6u);
        display_compact_value(56u, y + 22u, pv_routes[route].current_ma / 100,
                              "A", COLOR_WHITE, 6u);
    }

    draw_border(140u, 34u, 207u, 168u, COLOR_WHITE);
    LCD_Show_String_With_Chinese(150u, 38u, "逆变器", COLOR_YELLOW, COLOR_BLACK);
    uint16_t inverter_color = inverter.communication == INVERTER_COMM_NORMAL ? COLOR_CYAN : COLOR_GRAY;
    display_compact_value(144u, 60u, inverter.ac_voltage_tenth_v, "V", inverter_color, 7u);
    display_compact_value(144u, 80u, inverter.ac_frequency_tenth_hz, "Hz", inverter_color, 7u);
    display_compact_value(144u, 100u, inverter.ac_current_tenth_a, "A", inverter_color, 7u);
    (void)snprintf(text, sizeof(text), "%luW", (unsigned long)inverter.ac_power_w);
    LCD_Show_String_With_Chinese(144u, 120u, strlen(text) <= 7u ? text : "超限",
                                 inverter_color, COLOR_BLACK);

    draw_border(244u, 34u, 315u, 117u, COLOR_WHITE);
    LCD_Show_String_With_Chinese(248u, 38u, "电池", COLOR_YELLOW, COLOR_BLACK);
    display_overview_battery_data();
    draw_border(244u, 127u, 315u, 168u, COLOR_WHITE);
    LCD_Show_String_With_Chinese(248u, 131u, "负载", COLOR_YELLOW, COLOR_BLACK);
    LCD_Show_String_With_Chinese(264u, 149u, "--", COLOR_GRAY, COLOR_BLACK);

    LCD_Fill(0u, 176u, 319u, 177u, COLOR_GRAY);
    LCD_Show_String_With_Chinese(8u, 184u, "环境", COLOR_YELLOW, COLOR_BLACK);

    display_compact_temperature(56u, 184u, external_sensors.temperature_tenth_c, 5u);

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
    LCD_Show_String_With_Chinese(242u, 216u, "1/6", COLOR_CYAN, COLOR_BLACK);
    display_overview_animation(1u);
}

static void display_pv_page(void)
{
    char text[48], value[24];
    uint64_t total_w = 0u;

    LCD_Show_String_With_Chinese(8u, 8u, "三路MPPT", COLOR_YELLOW, COLOR_BLACK);
    LCD_Show_String_With_Chinese(278u, 8u, "3/6", COLOR_CYAN, COLOR_BLACK);

    for(uint8_t route = 0u; route < 3u; route++) {
        uint16_t y = 34u + route * 60u;
        uint64_t power_w = (uint64_t)pv_routes[route].voltage_mv *
                           (pv_routes[route].current_ma > 0 ? (uint32_t)pv_routes[route].current_ma : 0u) /
                           1000000u;
        total_w += power_w;
        draw_border(8u, y, 312u, y + 56u, COLOR_WHITE);
        (void)snprintf(text, sizeof(text), "PV%u", (unsigned int)(route + 1u));
        LCD_Show_String_With_Chinese(12u, y + 3u, text, COLOR_YELLOW, COLOR_BLACK);

        (void)snprintf(value, sizeof(value), "%u.%uV",
                       (unsigned int)(pv_routes[route].voltage_mv / 1000u),
                       (unsigned int)((pv_routes[route].voltage_mv / 100u) % 10u));
        (void)snprintf(text, sizeof(text), "电压:%s", strlen(value) <= 6u ? value : "超限");
        LCD_Show_String_With_Chinese(56u, y + 3u, text, COLOR_CYAN, COLOR_BLACK);
        format_value(value, sizeof(value), pv_routes[route].current_ma / 100, "A");
        const char *current = value[0] == ' ' ? value + 1 : value;
        (void)snprintf(text, sizeof(text), "电流:%s", strlen(current) <= 6u ? current : "超限");
        LCD_Show_String_With_Chinese(168u, y + 3u, text, COLOR_WHITE, COLOR_BLACK);

        if(power_w <= 999999u)
            (void)snprintf(text, sizeof(text), "功率:%luW", (unsigned long)power_w);
        else
            (void)snprintf(text, sizeof(text), "功率:超限");
        LCD_Show_String_With_Chinese(12u, y + 20u, text, COLOR_GREEN, COLOR_BLACK);
        if(pv_routes[route].energy_tenth_kwh <= 9999999u)
            (void)snprintf(text, sizeof(text), "累计发电量:%lu.%lukWh",
                           (unsigned long)(pv_routes[route].energy_tenth_kwh / 10u),
                           (unsigned long)(pv_routes[route].energy_tenth_kwh % 10u));
        else
            (void)snprintf(text, sizeof(text), "累计发电量:超限");
        LCD_Show_String_With_Chinese(128u, y + 20u, text, COLOR_GREEN, COLOR_BLACK);
        (void)snprintf(text, sizeof(text), "PV%u告警:--", (unsigned int)(route + 1u));
        LCD_Show_String_With_Chinese(12u, y + 37u, text, COLOR_GRAY, COLOR_BLACK);
    }

    if(total_w <= 999999999u)
        (void)snprintf(text, sizeof(text), "总功率  %luW", (unsigned long)total_w);
    else
        (void)snprintf(text, sizeof(text), "总功率  超限");
    LCD_Show_String_With_Chinese(18u, 218u, text, COLOR_GREEN, COLOR_BLACK);
}

static void display_sensor_page(void)
{
    char text[32];

    LCD_Show_String_With_Chinese(8u, 8u, "外部传感器", COLOR_YELLOW, COLOR_BLACK);
    LCD_Show_String_With_Chinese(278u, 8u, "6/6", COLOR_CYAN, COLOR_BLACK);

    draw_border(8u, 38u, 312u, 198u, COLOR_WHITE);

    LCD_Show_String_With_Chinese(18u, 52u, "温度", COLOR_WHITE, COLOR_BLACK);
    format_value(text, sizeof(text), external_sensors.temperature_tenth_c, "℃");
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

}

static uint8_t display_inverter_status(uint8_t force)
{
    static InverterCommunication previous = INVERTER_COMM_UNKNOWN;
    if(force || previous != inverter.communication) {
        const char *status = "系统告警:通讯--";
        uint16_t color = COLOR_GRAY;
        if(inverter.communication == INVERTER_COMM_ERROR) {
            status = "系统告警:通讯异常";
            color = COLOR_RED;
        } else if(inverter.communication == INVERTER_COMM_NORMAL) {
            status = "系统告警:通讯正常";
            color = COLOR_GREEN;
        }
        LCD_Fill(12u, 30u, 147u, 45u, COLOR_BLACK);
        LCD_Show_String_With_Chinese(12u, 30u, status, color, COLOR_BLACK);
        previous = inverter.communication;
        return 1u;
    }
    return 0u;
}

static void display_inverter_data(void)
{
    char text[64];
    uint16_t ac_color = inverter.communication == INVERTER_COMM_NORMAL ? COLOR_CYAN : COLOR_GRAY;
    uint16_t charger_color = inverter.communication == INVERTER_COMM_NORMAL ? COLOR_YELLOW : COLOR_GRAY;

    (void)snprintf(text, sizeof(text), "交流输出电压:%u.%uV",
                   (unsigned int)(inverter.ac_voltage_tenth_v / 10u),
                   (unsigned int)(inverter.ac_voltage_tenth_v % 10u));
    LCD_Show_String_With_Chinese(12u, 59u, text, ac_color, COLOR_BLACK);
    (void)snprintf(text, sizeof(text), "交流输出频率:%u.%uHz",
                   (unsigned int)(inverter.ac_frequency_tenth_hz / 10u),
                   (unsigned int)(inverter.ac_frequency_tenth_hz % 10u));
    LCD_Show_String_With_Chinese(12u, 80u, text, ac_color, COLOR_BLACK);
    (void)snprintf(text, sizeof(text), "交流输出总电流:%u.%uA",
                   (unsigned int)(inverter.ac_current_tenth_a / 10u),
                   (unsigned int)(inverter.ac_current_tenth_a % 10u));
    LCD_Show_String_With_Chinese(12u, 101u, text, ac_color, COLOR_BLACK);
    (void)snprintf(text, sizeof(text), "交流输出总功率:%luW",
                   (unsigned long)inverter.ac_power_w);
    LCD_Show_String_With_Chinese(12u, 122u, text, ac_color, COLOR_BLACK);
    (void)snprintf(text, sizeof(text), "充电枪输出电压:%u.%uV",
                   (unsigned int)(inverter.charger_voltage_tenth_v / 10u),
                   (unsigned int)(inverter.charger_voltage_tenth_v % 10u));
    LCD_Show_String_With_Chinese(12u, 171u, text, charger_color, COLOR_BLACK);
    (void)snprintf(text, sizeof(text), "充电枪输出功率:%luW",
                   (unsigned long)inverter.charger_power_w);
    LCD_Show_String_With_Chinese(12u, 196u, text, charger_color, COLOR_BLACK);
}

static void display_inverter_page(void)
{
    LCD_Show_String_With_Chinese(8u, 8u, "DC/AC逆变模块", COLOR_YELLOW, COLOR_BLACK);
    LCD_Show_String_With_Chinese(278u, 8u, "2/6", COLOR_CYAN, COLOR_BLACK);
    (void)display_inverter_status(1u);
    draw_border(8u, 52u, 311u, 149u, COLOR_WHITE);
    draw_border(8u, 158u, 311u, 230u, COLOR_WHITE);
    display_inverter_data();
}

static void display_page(uint8_t page)
{
    static uint8_t previous_page = UI_PAGE_COUNT;
    uint8_t reuse_battery_layout = previous_page == UI_PAGE_BATTERY ||
                                   previous_page == UI_PAGE_BATTERY_DETAIL;

    switch(previous_page) {
        case UI_PAGE_OVERVIEW:
            LCD_Fill(6u, 6u, 69u, 21u, COLOR_BLACK);
            LCD_Fill(249u, 6u, 280u, 21u, COLOR_BLACK);
            LCD_Fill(286u, 6u, 316u, 21u, COLOR_BLACK);
            LCD_Fill(0u, 28u, 319u, 29u, COLOR_BLACK);
            for(uint8_t route = 0u; route < 3u; route++) {
                uint16_t y = 34u + route * 46u;
                draw_border(4u, y, 106u, y + 42u, COLOR_BLACK);
                LCD_Fill(8u, y + 4u, 31u, y + 19u, COLOR_BLACK);
                LCD_Fill(8u, y + 22u, 103u, y + 37u, COLOR_BLACK);
                LCD_Fill(107u, y + 17u, 139u, y + 26u, COLOR_BLACK);
            }
            draw_border(140u, 34u, 207u, 168u, COLOR_BLACK);
            LCD_Fill(150u, 38u, 197u, 53u, COLOR_BLACK);
            for(uint16_t y = 60u; y <= 120u; y += 20u)
                LCD_Fill(144u, y, 199u, y + 15u, COLOR_BLACK);
            draw_border(244u, 34u, 315u, 117u, COLOR_BLACK);
            LCD_Fill(248u, 38u, 279u, 53u, COLOR_BLACK);
            LCD_Fill(248u, 56u, 311u, 109u, COLOR_BLACK);
            draw_border(244u, 127u, 315u, 168u, COLOR_BLACK);
            LCD_Fill(248u, 131u, 279u, 146u, COLOR_BLACK);
            LCD_Fill(264u, 149u, 279u, 164u, COLOR_BLACK);
            LCD_Fill(208u, 59u, 243u, 68u, COLOR_BLACK);
            LCD_Fill(208u, 91u, 243u, 100u, COLOR_BLACK);
            LCD_Fill(208u, 143u, 243u, 152u, COLOR_BLACK);
            LCD_Fill(0u, 176u, 319u, 177u, COLOR_BLACK);
            LCD_Fill(8u, 184u, 39u, 199u, COLOR_BLACK);
            LCD_Fill(56u, 184u, 111u, 199u, COLOR_BLACK);
            LCD_Fill(112u, 184u, 159u, 199u, COLOR_BLACK);
            LCD_Fill(176u, 184u, 239u, 199u, COLOR_BLACK);
            LCD_Fill(248u, 184u, 295u, 199u, COLOR_BLACK);
            LCD_Fill(0u, 207u, 319u, 208u, COLOR_BLACK);
            LCD_Fill(8u, 216u, 103u, 231u, COLOR_BLACK);
            LCD_Fill(242u, 216u, 305u, 231u, COLOR_BLACK);
            break;
        case UI_PAGE_PV:
            LCD_Fill(8u, 8u, 71u, 23u, COLOR_BLACK);
            LCD_Fill(278u, 8u, 309u, 23u, COLOR_BLACK);
            for(uint8_t route = 0u; route < 3u; route++) {
                uint16_t y = 34u + route * 60u;
                draw_border(8u, y, 312u, y + 56u, COLOR_BLACK);
                LCD_Fill(12u, y + 3u, 35u, y + 18u, COLOR_BLACK);
                LCD_Fill(56u, y + 3u, 143u, y + 18u, COLOR_BLACK);
                LCD_Fill(168u, y + 3u, 255u, y + 18u, COLOR_BLACK);
                LCD_Fill(12u, y + 20u, 107u, y + 35u, COLOR_BLACK);
                LCD_Fill(128u, y + 20u, 303u, y + 35u, COLOR_BLACK);
                LCD_Fill(12u, y + 37u, 91u, y + 52u, COLOR_BLACK);
            }
            LCD_Fill(18u, 218u, 161u, 233u, COLOR_BLACK);
            break;
        case UI_PAGE_BATTERY:
        case UI_PAGE_BATTERY_DETAIL:
            battery_page_clear();
            if(page != UI_PAGE_BATTERY && page != UI_PAGE_BATTERY_DETAIL) {
                LCD_Fill(8u, 8u, 151u, 23u, COLOR_BLACK);
                LCD_Fill(278u, 8u, 309u, 23u, COLOR_BLACK);
                draw_border(8u, 38u, 311u, 120u, COLOR_BLACK);
                draw_border(8u, 132u, 311u, 230u, COLOR_BLACK);
            }
            break;
        case UI_PAGE_INVERTER:
            LCD_Fill(8u, 8u, 111u, 23u, COLOR_BLACK);
            LCD_Fill(278u, 8u, 309u, 23u, COLOR_BLACK);
            LCD_Fill(12u, 30u, 147u, 45u, COLOR_BLACK);
            draw_border(8u, 52u, 311u, 149u, COLOR_BLACK);
            draw_border(8u, 158u, 311u, 230u, COLOR_BLACK);
            for(uint16_t y = 59u; y <= 122u; y += 21u)
                LCD_Fill(12u, y, 219u, y + 15u, COLOR_BLACK);
            LCD_Fill(12u, 171u, 219u, 186u, COLOR_BLACK);
            LCD_Fill(12u, 196u, 219u, 211u, COLOR_BLACK);
            break;
        case UI_PAGE_SENSOR:
            LCD_Fill(8u, 8u, 167u, 23u, COLOR_BLACK);
            LCD_Fill(278u, 8u, 309u, 23u, COLOR_BLACK);
            for(uint16_t y = 52u; y <= 142u; y += 30u) {
                LCD_Fill(18u, y, 65u, y + 15u, COLOR_BLACK);
                LCD_Fill(180u, y, 299u, y + 15u, COLOR_BLACK);
            }
            draw_border(8u, 38u, 312u, 198u, COLOR_BLACK);
            break;
        default:
            break;
    }

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
        case UI_PAGE_INVERTER:
            display_inverter_page();
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
        uint8_t page_navigation_blocked = 0u;

        if(KEY_ESC_PRESSED()) {
            if(key_esc_released) {
                delay_key_debounce();
                if(KEY_ESC_PRESSED()) {
                    key_esc_released = 0u;
                    if(showing_startup || current_page != UI_PAGE_OVERVIEW) {
                        if(showing_startup) {
                            LCD_Fill_Fast(COLOR_BLACK);
                            showing_startup = 0u;
                        }
                        current_page = UI_PAGE_OVERVIEW;
                        display_page(current_page);
                        battery_snapshot = battery_ui_snapshot();
                    }
                    key_up_released = 0u;
                    key_down_released = 0u;
                    page_navigation_blocked = 1u;
                }
            }
        } else {
            key_esc_released = 1u;
        }

        if(showing_startup) {
            delay_ms(10u);
            continue;
        }

        if(!page_navigation_blocked) {
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
        }

        if(battery_ui_changed(&battery_snapshot)) {
            if(current_page == UI_PAGE_OVERVIEW) {
                display_overview_battery_data();
            }
            battery_snapshot = battery_ui_snapshot();
        }
        if(current_page == UI_PAGE_OVERVIEW) {
            display_overview_animation(0u);
        }
        if(current_page == UI_PAGE_INVERTER) {
            if(display_inverter_status(0u)) {
                display_inverter_data();
            }
        }
        if(current_page == UI_PAGE_BATTERY || current_page == UI_PAGE_BATTERY_DETAIL) {
            battery_page_update(current_page == UI_PAGE_BATTERY ? BATTERY_PAGE_SUMMARY : BATTERY_PAGE_DETAIL);
        }

        delay_ms(10u);
    }
}
