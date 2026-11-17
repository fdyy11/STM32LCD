#ifndef BATTERY_RS485_H
#define BATTERY_RS485_H

#include "stm32f10x.h"
#include <stdint.h>

#ifndef BATTERY_GROUP_ADDRESS
#define BATTERY_GROUP_ADDRESS 0u
#endif

#define BATTERY_POLL_INTERVAL_MS 500u

typedef enum {
    BATTERY_ERROR_NONE = 0,
    BATTERY_ERROR_TIMEOUT,
    BATTERY_ERROR_FRAME,
    BATTERY_ERROR_CHECKSUM,
    BATTERY_ERROR_PAYLOAD,
    BATTERY_ERROR_DEVICE
} BatteryError;

typedef enum {
    BATTERY_ANALOG_TOTAL_VOLTAGE = 0,
    BATTERY_ANALOG_TOTAL_CURRENT,
    BATTERY_ANALOG_SOC,
    BATTERY_ANALOG_AVERAGE_CYCLES,
    BATTERY_ANALOG_MAX_CYCLES,
    BATTERY_ANALOG_AVERAGE_SOH,
    BATTERY_ANALOG_MIN_SOH,
    BATTERY_ANALOG_MAX_CELL_VOLTAGE,
    BATTERY_ANALOG_MAX_CELL_ADDRESS,
    BATTERY_ANALOG_MIN_CELL_VOLTAGE,
    BATTERY_ANALOG_MIN_CELL_ADDRESS,
    BATTERY_ANALOG_AVERAGE_CELL_TEMPERATURE,
    BATTERY_ANALOG_MAX_CELL_TEMPERATURE,
    BATTERY_ANALOG_MAX_CELL_TEMPERATURE_ADDRESS,
    BATTERY_ANALOG_MIN_CELL_TEMPERATURE,
    BATTERY_ANALOG_MIN_CELL_TEMPERATURE_ADDRESS,
    BATTERY_ANALOG_AVERAGE_MOS_TEMPERATURE,
    BATTERY_ANALOG_MAX_MOS_TEMPERATURE,
    BATTERY_ANALOG_MAX_MOS_TEMPERATURE_ADDRESS,
    BATTERY_ANALOG_MIN_MOS_TEMPERATURE,
    BATTERY_ANALOG_MIN_MOS_TEMPERATURE_ADDRESS,
    BATTERY_ANALOG_AVERAGE_BMS_TEMPERATURE,
    BATTERY_ANALOG_MAX_BMS_TEMPERATURE,
    BATTERY_ANALOG_MAX_BMS_TEMPERATURE_ADDRESS,
    BATTERY_ANALOG_MIN_BMS_TEMPERATURE,
    BATTERY_ANALOG_MIN_BMS_TEMPERATURE_ADDRESS
} BatteryAnalogField;

typedef struct {
    uint32_t available_mask;
    uint16_t system_voltage_mv;
    int32_t system_current_ma;
    uint8_t system_soc_percent;
    uint16_t average_cycle_count;
    uint16_t max_cycle_count;
    uint8_t average_soh_percent;
    uint8_t min_soh_percent;
    uint16_t max_cell_voltage_mv;
    uint16_t max_cell_address;
    uint16_t min_cell_voltage_mv;
    uint16_t min_cell_address;
    int32_t average_cell_temperature_centi_c;
    int32_t max_cell_temperature_centi_c;
    uint16_t max_cell_temperature_address;
    int32_t min_cell_temperature_centi_c;
    uint16_t min_cell_temperature_address;
    int32_t average_mos_temperature_centi_c;
    int32_t max_mos_temperature_centi_c;
    uint16_t max_mos_temperature_address;
    int32_t min_mos_temperature_centi_c;
    uint16_t min_mos_temperature_address;
    int32_t average_bms_temperature_centi_c;
    int32_t max_bms_temperature_centi_c;
    uint16_t max_bms_temperature_address;
    int32_t min_bms_temperature_centi_c;
    uint16_t min_bms_temperature_address;
    uint32_t updated_at_ms;
} BatteryAnalogData;

typedef struct {
    uint8_t alarm_status_1;
    uint8_t alarm_status_2;
    uint8_t protection_status_1;
    uint8_t protection_status_2;
    uint8_t available_mask;
    uint32_t updated_at_ms;
} BatteryAlarmData;

typedef enum {
    BATTERY_ALARM1_MODULE_VOLTAGE_HIGH = 0x80u,
    BATTERY_ALARM1_MODULE_VOLTAGE_LOW = 0x40u,
    BATTERY_ALARM1_CELL_VOLTAGE_HIGH = 0x20u,
    BATTERY_ALARM1_CELL_VOLTAGE_LOW = 0x10u,
    BATTERY_ALARM1_CELL_TEMPERATURE_HIGH = 0x08u,
    BATTERY_ALARM1_CELL_TEMPERATURE_LOW = 0x04u,
    BATTERY_ALARM1_MOS_TEMPERATURE_HIGH = 0x02u,
    BATTERY_ALARM1_CELL_VOLTAGE_CONSISTENCY = 0x01u,
    BATTERY_ALARM2_CELL_TEMPERATURE_CONSISTENCY = 0x80u,
    BATTERY_ALARM2_CHARGE_OVERCURRENT = 0x40u,
    BATTERY_ALARM2_DISCHARGE_OVERCURRENT = 0x20u,
    BATTERY_ALARM2_INTERNAL_COMMUNICATION = 0x10u,
    BATTERY_PROTECTION1_MODULE_OVERVOLTAGE = 0x80u,
    BATTERY_PROTECTION1_MODULE_UNDERVOLTAGE = 0x40u,
    BATTERY_PROTECTION1_CELL_OVERVOLTAGE = 0x20u,
    BATTERY_PROTECTION1_CELL_UNDERVOLTAGE = 0x10u,
    BATTERY_PROTECTION1_CELL_OVERTEMPERATURE = 0x08u,
    BATTERY_PROTECTION1_CELL_UNDERTEMPERATURE = 0x04u,
    BATTERY_PROTECTION1_MOS_OVERTEMPERATURE = 0x02u,
    BATTERY_PROTECTION2_CHARGE_OVERCURRENT = 0x40u,
    BATTERY_PROTECTION2_DISCHARGE_OVERCURRENT = 0x20u,
    BATTERY_PROTECTION2_SYSTEM_FAULT = 0x08u
} BatteryAlarmMask;

typedef enum {
    BATTERY_CHARGE_ENABLED = 0x80u,
    BATTERY_DISCHARGE_ENABLED = 0x40u,
    BATTERY_CHARGE_IMMEDIATELY = 0x20u,
    BATTERY_FULL_CHARGE_REQUEST = 0x10u
} BatteryChargeStatusMask;

typedef struct {
    uint8_t available_mask;
    uint16_t charge_voltage_limit_mv;
    uint16_t discharge_voltage_limit_mv;
    uint16_t max_charge_current_deciamp;
    uint16_t max_discharge_current_deciamp;
    uint8_t charge_discharge_status;
    uint32_t updated_at_ms;
} BatteryChargeData;

typedef struct {
    uint8_t battery_count;
    uint8_t available;
    uint32_t updated_at_ms;
} BatteryBasicData;

typedef struct {
    BatteryBasicData basic;
    BatteryAnalogData analog;
    BatteryAlarmData alarms;
    BatteryChargeData charge;
    uint8_t address;
    uint8_t address_valid;
    uint8_t last_cid2;
    uint8_t last_rtn;
    BatteryError last_error;
    uint32_t successful_responses;
    uint32_t timeout_count;
    uint32_t frame_error_count;
    uint32_t checksum_error_count;
    uint32_t device_error_count;
    uint8_t polling_active;
} BatteryData;

extern BatteryData g_battery_data;

void Battery_RS485_Init(void);
void Battery_RS485_PollService(void);
void Battery_RS485_SysTickHandler(void);
uint32_t Battery_RS485_GetTickMs(void);
void Battery_RS485_SendBytes(const uint8_t *data, uint16_t length);
void USART3_IRQHandler(void);

#endif
