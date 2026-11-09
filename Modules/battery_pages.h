#ifndef BATTERY_PAGES_H
#define BATTERY_PAGES_H

#include <stdint.h>

typedef enum {
    BATTERY_PAGE_SUMMARY,
    BATTERY_PAGE_DETAIL
} BatteryPage;

void display_battery_page(BatteryPage page, uint8_t reuse_layout);
void battery_page_update(BatteryPage page);

#endif
