#include "battery_pages.h"
#include "battery_rs485.h"
#include "lcd_st7789.h"
#include <stdio.h>
#include <string.h>

static void draw_border(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color)
{
    LCD_Fill(x1, y1, x2, y1 + 1u, color);
    LCD_Fill(x1, y2 - 1u, x2, y2, color);
    LCD_Fill(x1, y1, x1 + 1u, y2, color);
    LCD_Fill(x2 - 1u, y1, x2, y2, color);
}

typedef struct {
    char text[96];
    uint16_t color;
    uint16_t x;
    uint16_t y;
    uint16_t width;
    uint8_t valid;
} BatteryPageLine;

static BatteryPageLine battery_page_lines[8];

static uint16_t battery_text_width(const char *text)
{
    uint16_t width = 0u;

    while(*text != '\0') {
        uint8_t ch = (uint8_t)*text;
        if(ch < 0x80u) {
            width += 8u;
            text++;
        } else if(ch >= 0xE0u && ch <= 0xEFu &&
                  text[1] != '\0' && text[2] != '\0') {
            width += 16u;
            text += 3;
        } else if(ch >= 0xC0u && ch < 0xE0u && text[1] != '\0') {
            width += 16u;
            text += 2;
        } else {
            text++;
        }
    }

    return width;
}

static void battery_page_part(uint8_t index, uint16_t x, uint16_t y, uint16_t width,
                              const char *text, uint16_t color)
{
    BatteryPageLine *line = &battery_page_lines[index];
    if(!line->valid || line->color != color || strcmp(line->text, text) != 0) {
        uint16_t old_width = line->valid ? battery_text_width(line->text) : 0u;
        uint16_t new_width = battery_text_width(text);
        if(new_width > width) new_width = width;

        LCD_Show_String_With_Chinese(x, y, text, color, COLOR_BLACK);
        if(old_width > new_width) {
            uint16_t clear_x = x + new_width;
            uint16_t clear_end = x + old_width - 1u;
            if(clear_end > x + width - 1u) clear_end = x + width - 1u;
            if(clear_x <= clear_end) {
                LCD_Fill(clear_x, y, clear_end, y + 15u, COLOR_BLACK);
            }
        }
        (void)snprintf(line->text, sizeof(line->text), "%s", text);
        line->color = color;
        line->x = x;
        line->y = y;
        line->width = width;
        line->valid = 1u;
    }
}

static void battery_page_line(uint8_t index, uint16_t y, const char *text, uint16_t color)
{
    battery_page_part(index, 12u, y, 296u, text, color);
}

static uint8_t battery_field_available(BatteryAnalogField field)
{
    return (g_battery_data.analog.available_mask & ((uint32_t)1u << field)) != 0u;
}

static void battery_percent(char *out, uint16_t size, uint8_t value, uint8_t available)
{
    if(available) {
        (void)snprintf(out, size, "%u%%", (unsigned int)value);
    } else {
        (void)snprintf(out, size, "--");
    }
}

static void battery_temperature(char *out, uint16_t size, int32_t centi_c, uint8_t available)
{
    if(available) {
        int32_t tenths = centi_c / 10;
        uint32_t magnitude = tenths < 0 ? (uint32_t)(-tenths) : (uint32_t)tenths;
        (void)snprintf(out, size, "%s%lu.%lu\xE2\x84\x83",
                       tenths < 0 ? "-" : "", (unsigned long)(magnitude / 10u),
                       (unsigned long)(magnitude % 10u));
    } else {
        (void)snprintf(out, size, "--");
    }
}

static void battery_alarm_text(char *out, uint8_t value, uint8_t available)
{
    uint8_t pos = 0u;
    if(!available) {
        (void)snprintf(out, 24u, "--");
    } else if(value == 0u) {
        (void)snprintf(out, 24u, "\xE6\xAD\xA3\xE5\xB8\xB8");
    } else {
        for(uint8_t bit = 0u; bit < 8u; bit++) {
            if((value & (1u << bit)) != 0u) {
                if(pos != 0u) out[pos++] = '/';
                out[pos++] = (char)('0' + bit);
            }
        }
        out[pos] = '\0';
    }
}

void battery_page_update(BatteryPage page)
{
    char text[96], first[24], second[24];
    BatteryAnalogData *analog = &g_battery_data.analog;

    if(page == BATTERY_PAGE_SUMMARY) {
        battery_percent(first, sizeof(first), analog->system_soc_percent,
                        g_battery_data.address_valid && battery_field_available(BATTERY_ANALOG_SOC));
        (void)snprintf(text, sizeof(text), "\xE7\x94\xB5\xE6\xB1\xA0\xE5\x89\xA9\xE4\xBD\x99\xE5\xAE\xB9\xE9\x87\x8F:%s", first);
        battery_page_line(0u, 48u, text,
                          g_battery_data.address_valid && battery_field_available(BATTERY_ANALOG_SOC) ? COLOR_GREEN : COLOR_GRAY);

        if(g_battery_data.address_valid && battery_field_available(BATTERY_ANALOG_TOTAL_VOLTAGE)) {
            (void)snprintf(text, sizeof(text), "\xE7\x94\xB5\xE6\xB1\xA0\xE7\x94\xB5\xE5\x8E\x8B:%u.%02uV",
                           (unsigned int)(analog->system_voltage_mv / 1000u),
                           (unsigned int)((analog->system_voltage_mv / 10u) % 100u));
        } else {
            (void)snprintf(text, sizeof(text), "\xE7\x94\xB5\xE6\xB1\xA0\xE7\x94\xB5\xE5\x8E\x8B:--V");
        }
        battery_page_line(1u, 71u, text,
                          g_battery_data.address_valid && battery_field_available(BATTERY_ANALOG_TOTAL_VOLTAGE) ? COLOR_CYAN : COLOR_GRAY);

        if(g_battery_data.address_valid && battery_field_available(BATTERY_ANALOG_TOTAL_CURRENT)) {
            int32_t current = analog->system_current_ma;
            uint32_t charge = current > 0 ? (uint32_t)current : 0u;
            uint32_t discharge = current < 0 ? (uint32_t)(-current) : 0u;
            (void)snprintf(first, sizeof(first), "\xE5\x85\x85\xE7\x94\xB5:%lu.%02luA",
                           (unsigned long)(charge / 1000u), (unsigned long)((charge / 10u) % 100u));
            (void)snprintf(second, sizeof(second), "\xE6\x94\xBE\xE7\x94\xB5:%lu.%02luA",
                           (unsigned long)(discharge / 1000u), (unsigned long)((discharge / 10u) % 100u));
        } else {
            (void)snprintf(first, sizeof(first), "\xE5\x85\x85\xE7\x94\xB5:--A");
            (void)snprintf(second, sizeof(second), "\xE6\x94\xBE\xE7\x94\xB5:--A");
        }
        battery_page_part(2u, 12u, 94u, 138u, first,
                          g_battery_data.address_valid && battery_field_available(BATTERY_ANALOG_TOTAL_CURRENT) ? COLOR_GREEN : COLOR_GRAY);
        battery_page_part(3u, 150u, 94u, 158u, second,
                          g_battery_data.address_valid && battery_field_available(BATTERY_ANALOG_TOTAL_CURRENT) ? COLOR_ORANGE : COLOR_GRAY);

        battery_percent(first, sizeof(first), analog->average_soh_percent,
                        battery_field_available(BATTERY_ANALOG_AVERAGE_SOH));
        battery_percent(second, sizeof(second), analog->min_soh_percent,
                        battery_field_available(BATTERY_ANALOG_MIN_SOH));
        (void)snprintf(text, sizeof(text), "\xE5\xB9\xB3\xE5\x9D\x87SOH/\xE6\x9C\x80\xE5\xB0\x8FSOH:%s/%s", first, second);
        battery_page_line(4u, 142u, text,
                          battery_field_available(BATTERY_ANALOG_AVERAGE_SOH) || battery_field_available(BATTERY_ANALOG_MIN_SOH) ? COLOR_GREEN : COLOR_GRAY);

        if(!g_battery_data.address_valid || !battery_field_available(BATTERY_ANALOG_TOTAL_CURRENT))
            (void)snprintf(first, sizeof(first), "--");
        else if(analog->system_current_ma > 0)
            (void)snprintf(first, sizeof(first), "\xE5\x85\x85\xE7\x94\xB5");
        else if(analog->system_current_ma < 0)
            (void)snprintf(first, sizeof(first), "\xE6\x94\xBE\xE7\x94\xB5");
        else
            (void)snprintf(first, sizeof(first), "\xE7\xA9\xBA\xE9\x97\xB2");
        (void)snprintf(text, sizeof(text), "\xE7\x94\xB5\xE6\xB1\xA0\xE7\x8A\xB6\xE6\x80\x81:%s", first);
        battery_page_line(5u, 163u, text,
                          !g_battery_data.address_valid || !battery_field_available(BATTERY_ANALOG_TOTAL_CURRENT) ? COLOR_GRAY :
                          analog->system_current_ma > 0 ? COLOR_GREEN :
                          analog->system_current_ma < 0 ? COLOR_ORANGE : COLOR_CYAN);

        (void)snprintf(text, sizeof(text), "\xE9\x80\x9A\xE8\xAE\xAF\xE6\x83\x85\xE5\x86\xB5:%s",
                       g_battery_data.last_error != BATTERY_ERROR_NONE
                           ? "\xE5\xBC\x82\xE5\xB8\xB8"
                           : g_battery_data.successful_responses != 0u
                               ? "\xE6\xAD\xA3\xE5\xB8\xB8" : "--");
        battery_page_line(6u, 184u, text,
                          g_battery_data.last_error != BATTERY_ERROR_NONE ? COLOR_RED :
                          g_battery_data.successful_responses != 0u ? COLOR_GREEN : COLOR_GRAY);

        if(g_battery_data.basic.available)
            (void)snprintf(first, sizeof(first), "%u", (unsigned int)g_battery_data.basic.battery_count);
        else
            (void)snprintf(first, sizeof(first), "--");
        (void)snprintf(text, sizeof(text), "\xE7\x94\xB5\xE6\xB1\xA0\xE6\x95\xB0\xE9\x87\x8F:%s", first);
        battery_page_line(7u, 205u, text, g_battery_data.basic.available ? COLOR_YELLOW : COLOR_GRAY);
    } else {
        static const char *const labels[] = {
            "\xE5\x8D\x95\xE8\x8A\xAF\xE6\x9C\x80\xE9\xAB\x98/\xE5\xB9\xB3\xE5\x9D\x87\xE6\xB8\xA9\xE5\xBA\xA6:",
            "MOS\xE6\x9C\x80\xE9\xAB\x98/\xE5\xB9\xB3\xE5\x9D\x87\xE6\xB8\xA9\xE5\xBA\xA6:",
            "BMS\xE6\x9C\x80\xE9\xAB\x98/\xE5\xB9\xB3\xE5\x9D\x87\xE6\xB8\xA9\xE5\xBA\xA6:"
        };
        const int32_t max_values[] = {analog->max_cell_temperature_centi_c, analog->max_mos_temperature_centi_c, analog->max_bms_temperature_centi_c};
        const int32_t avg_values[] = {analog->average_cell_temperature_centi_c, analog->average_mos_temperature_centi_c, analog->average_bms_temperature_centi_c};
        const BatteryAnalogField max_fields[] = {BATTERY_ANALOG_MAX_CELL_TEMPERATURE, BATTERY_ANALOG_MAX_MOS_TEMPERATURE, BATTERY_ANALOG_MAX_BMS_TEMPERATURE};
        const BatteryAnalogField avg_fields[] = {BATTERY_ANALOG_AVERAGE_CELL_TEMPERATURE, BATTERY_ANALOG_AVERAGE_MOS_TEMPERATURE, BATTERY_ANALOG_AVERAGE_BMS_TEMPERATURE};
        const uint8_t alarm_values[] = {g_battery_data.alarms.alarm_status_1, g_battery_data.alarms.alarm_status_2, g_battery_data.alarms.protection_status_1, g_battery_data.alarms.protection_status_2};
        static const char *const alarm_labels[] = {"\xE5\x91\x8A\xE8\xAD\xA6" "1:", "\xE5\x91\x8A\xE8\xAD\xA6" "2:", "\xE4\xBF\x9D\xE6\x8A\xA4" "1:", "\xE4\xBF\x9D\xE6\x8A\xA4" "2:"};
        static const uint16_t upper_y[] = {48u, 71u, 94u};
        static const uint16_t lower_y[] = {142u, 163u, 184u, 205u};
        for(uint8_t i = 0u; i < 3u; i++) {
            battery_temperature(first, sizeof(first), max_values[i], battery_field_available(max_fields[i]));
            battery_temperature(second, sizeof(second), avg_values[i], battery_field_available(avg_fields[i]));
            (void)snprintf(text, sizeof(text), "%s%s/%s", labels[i], first, second);
            battery_page_line(i, upper_y[i], text,
                              battery_field_available(max_fields[i]) || battery_field_available(avg_fields[i]) ? COLOR_CYAN : COLOR_GRAY);
        }
        for(uint8_t i = 0u; i < 4u; i++) {
            uint8_t available = (g_battery_data.alarms.available_mask & (1u << i)) != 0u;
            battery_alarm_text(first, alarm_values[i], available);
            (void)snprintf(text, sizeof(text), "%s%s", alarm_labels[i], first);
            battery_page_line((uint8_t)(i + 3u), lower_y[i], text,
                              !available ? COLOR_GRAY : alarm_values[i] != 0u ? COLOR_RED : COLOR_GREEN);
        }
    }
}

void display_battery_page(BatteryPage page, uint8_t reuse_layout)
{
    if(reuse_layout) {
        battery_page_clear();
    } else {
        LCD_Show_String_With_Chinese(8u, 8u, "\xE7\x94\xB5\xE6\xB1\xA0\xE8\xAF\xA6\xE6\x83\x85", COLOR_YELLOW, COLOR_BLACK);
        draw_border(8u, 38u, 311u, 120u, COLOR_WHITE);
        draw_border(8u, 132u, 311u, 230u, COLOR_WHITE);
    }
    LCD_Show_String_With_Chinese(278u, 8u, page == BATTERY_PAGE_SUMMARY ? "4/6" : "5/6", COLOR_CYAN, COLOR_BLACK);
    memset(battery_page_lines, 0, sizeof(battery_page_lines));
    battery_page_update(page);
}

void battery_page_clear(void)
{
    for(uint8_t i = 0u; i < sizeof(battery_page_lines) / sizeof(battery_page_lines[0]); i++) {
        BatteryPageLine *line = &battery_page_lines[i];
        if(line->valid) {
            uint16_t width = battery_text_width(line->text);
            if(width > line->width) width = line->width;
            if(width != 0u) {
                LCD_Fill(line->x, line->y, line->x + width - 1u,
                         line->y + 15u, COLOR_BLACK);
            }
        }
        line->valid = 0u;
        line->text[0] = '\0';
    }
}
