#include "battery_rs485.h"

#define BATTERY_UART_BAUD_RATE 9600u
#define BATTERY_RESPONSE_TIMEOUT_MS 500u
#define BATTERY_INTERFRAME_GAP_MS 10u
#define BATTERY_RETRY_LIMIT 2u
#define BATTERY_MAX_INFO_BYTES (33u + 16u * 125u)
#define BATTERY_MAX_FRAME_BYTES (18u + 2u * BATTERY_MAX_INFO_BYTES)
#define BATTERY_ASCII_HEX_SIZE (2u * BATTERY_MAX_INFO_BYTES)
#define BATTERY_LONG_RESPONSE_TIMEOUT_MS 5000u

#define BATTERY_SOH 0x7Eu
#define BATTERY_EOI 0x0Du
#define BATTERY_CID1 0x46u
#define BATTERY_RS485_DIRECTION_PIN 13u
#define BATTERY_RS485_DIRECTION_HIGH_IS_TX 1u

typedef enum {
    POLL_IDLE = 0,
    POLL_WAIT_RESPONSE,
    POLL_INTERFRAME_GAP
} BatteryPollState;

typedef enum {
    FRAME_OK = 0,
    FRAME_INVALID,
    FRAME_BAD_CHECKSUM,
    FRAME_BAD_PAYLOAD
} BatteryFrameResult;

static const uint8_t poll_commands[] = {0x61u, 0x62u, 0x63u, 0x60u};

static volatile uint32_t battery_tick_ms;
static volatile uint8_t rx_frame[BATTERY_MAX_FRAME_BYTES];
static volatile uint16_t rx_frame_length;
static volatile uint16_t rx_frame_count;
static volatile uint8_t rx_frame_ready;
static volatile uint8_t rx_receiving;
static volatile uint32_t rx_last_byte_ms;
static uint32_t response_started_ms;
static BatteryPollState poll_state;
static uint32_t next_action_ms;
static uint32_t send_led_pulse_deadline;
static uint8_t command_index;
static uint8_t retry_count;
static uint8_t address_candidate;
static uint8_t address_fallback_used;

BatteryData g_battery_data;

static uint8_t time_reached(uint32_t now, uint32_t deadline) {
    return (int32_t)(now - deadline) >= 0;
}

static uint8_t hex_digit(uint8_t value) {
    value &= 0x0Fu;
    return (value < 10u) ? (uint8_t)('0' + value) : (uint8_t)('A' + value - 10u);
}

static uint8_t parse_hex_digit(uint8_t value, uint8_t *digit) {
    if(value >= '0' && value <= '9') {
        *digit = (uint8_t)(value - '0');
        return 1u;
    }
    if(value >= 'A' && value <= 'F') {
        *digit = (uint8_t)(value - 'A' + 10u);
        return 1u;
    }
    if(value >= 'a' && value <= 'f') {
        *digit = (uint8_t)(value - 'a' + 10u);
        return 1u;
    }
    return 0u;
}

static void append_hex_byte(uint8_t *frame, uint16_t *position, uint8_t value) {
    frame[(*position)++] = hex_digit((uint8_t)(value >> 4));
    frame[(*position)++] = hex_digit(value);
}

static uint8_t read_hex_value(const volatile uint8_t *frame, uint16_t position,
                              uint8_t digit_count, uint32_t *value) {
    uint32_t parsed = 0u;
    for(uint8_t i = 0u; i < digit_count; i++) {
        uint8_t digit;
        if(!parse_hex_digit(frame[position + i], &digit)) {
            return 0u;
        }
        parsed = (parsed << 4) | digit;
    }
    *value = parsed;
    return 1u;
}

static uint16_t length_checksum(uint16_t lenid) {
    uint16_t nibble_sum = (uint16_t)(((lenid >> 8) & 0x0Fu) +
                                     ((lenid >> 4) & 0x0Fu) +
                                     (lenid & 0x0Fu));
    return (uint16_t)((16u - (nibble_sum & 0x0Fu)) & 0x0Fu);
}

static uint16_t make_request(uint8_t address, uint8_t cid2, uint8_t *frame) {
    uint16_t position = 1u;
    uint16_t checksum = 0u;

    frame[0] = BATTERY_SOH;
    append_hex_byte(frame, &position, 0x20u);
    append_hex_byte(frame, &position, address);
    append_hex_byte(frame, &position, BATTERY_CID1);
    append_hex_byte(frame, &position, cid2);
    frame[position++] = '0';
    frame[position++] = '0';
    frame[position++] = '0';
    frame[position++] = '0';

    for(uint16_t i = 1u; i < position; i++) {
        checksum = (uint16_t)(checksum + frame[i]);
    }
    checksum = (uint16_t)(0u - checksum);
    append_hex_byte(frame, &position, (uint8_t)(checksum >> 8));
    append_hex_byte(frame, &position, (uint8_t)checksum);
    frame[position++] = BATTERY_EOI;
    return position;
}

static uint8_t group_address_offset(void) {
    return (uint8_t)((BATTERY_GROUP_ADDRESS & 0x07u) << 4);
}

static uint8_t current_address(void) {
    if(g_battery_data.address_valid) {
        return g_battery_data.address;
    }
    return (uint8_t)(group_address_offset() +
                     ((address_candidate == 0u) ? 0x12u : 0x02u));
}

static uint32_t apb1_clock_hz(void) {
    static const uint8_t apb_prescalers[8] = {1u, 1u, 1u, 1u, 2u, 4u, 8u, 16u};
    uint8_t prescaler_bits = (uint8_t)((RCC->CFGR >> 8) & 0x07u);
    return SystemCoreClock / apb_prescalers[prescaler_bits];
}

static void reset_receive_frame(void) {
    NVIC_DisableIRQ(USART3_IRQn);
    rx_frame_length = 0u;
    rx_frame_count = 0u;
    rx_frame_ready = 0u;
    rx_receiving = 0u;
    NVIC_EnableIRQ(USART3_IRQn);
}

static void rs485_direction_delay(void) {
    /* MAX485EESA+ direction pins need setup/hold time before and after the UART bytes. */
    for(volatile uint32_t i = 0u; i < 70000u; i++) {
        __asm__("nop");
    }
}

static void rs485_set_receive(void) {
#if BATTERY_RS485_DIRECTION_HIGH_IS_TX
    GPIOB->BSRR = 1u << (BATTERY_RS485_DIRECTION_PIN + 16u);
#else
    GPIOB->BSRR = 1u << BATTERY_RS485_DIRECTION_PIN;
#endif
}

static void rs485_set_transmit(void) {
#if BATTERY_RS485_DIRECTION_HIGH_IS_TX
    GPIOB->BSRR = 1u << BATTERY_RS485_DIRECTION_PIN;
#else
    GPIOB->BSRR = 1u << (BATTERY_RS485_DIRECTION_PIN + 16u);
#endif
}

static void begin_rs485_transmit(void) {
    USART3->CR1 &= ~USART_CR1_RE;
    (void)USART3->SR;
    (void)USART3->DR;
    rs485_set_transmit();
    rs485_direction_delay();
}

static void end_rs485_transmit(void) {
    rs485_direction_delay();
    rs485_set_receive();
    (void)USART3->SR;
    (void)USART3->DR;
    USART3->CR1 |= USART_CR1_RE | USART_CR1_RXNEIE;
}

static uint8_t take_received_frame(uint8_t *frame, uint16_t *length) {
    uint8_t ready;
    NVIC_DisableIRQ(USART3_IRQn);
    ready = rx_frame_ready;
    if(ready) {
        *length = rx_frame_length;
        for(uint16_t i = 0u; i < rx_frame_length; i++) {
            frame[i] = rx_frame[i];
        }
        rx_frame_length = 0u;
        rx_frame_count = 0u;
        rx_frame_ready = 0u;
        rx_receiving = 0u;
    }
    NVIC_EnableIRQ(USART3_IRQn);
    return ready;
}

static void send_request(uint8_t cid2) {
    uint8_t frame[18];
    uint16_t length = make_request(current_address(), cid2, frame);

    reset_receive_frame();
    begin_rs485_transmit();
    GPIOB->BSRR = 1u << 0u;
    send_led_pulse_deadline = battery_tick_ms + 100u;
    for(uint16_t i = 0u; i < length; i++) {
        while((USART3->SR & USART_SR_TXE) == 0u) {
        }
        USART3->DR = frame[i];
    }
    while((USART3->SR & USART_SR_TC) == 0u) {
    }
    end_rs485_transmit();

    g_battery_data.last_cid2 = cid2;
    next_action_ms = battery_tick_ms + BATTERY_RESPONSE_TIMEOUT_MS;
    response_started_ms = battery_tick_ms;
    poll_state = POLL_WAIT_RESPONSE;
}

void Battery_RS485_SendBytes(const uint8_t *data, uint16_t length) {
    begin_rs485_transmit();
    for(uint16_t i = 0u; i < length; i++) {
        while((USART3->SR & USART_SR_TXE) == 0u) {
        }
        USART3->DR = data[i];
    }
    while((USART3->SR & USART_SR_TC) == 0u) {
    }
    end_rs485_transmit();
}

static uint16_t read_u16_be(const uint8_t *info, uint16_t offset) {
    return (uint16_t)(((uint16_t)info[offset] << 8) | info[offset + 1u]);
}

static int32_t read_s16_be(const uint8_t *info, uint16_t offset) {
    uint16_t raw = read_u16_be(info, offset);
    return (raw < 0x8000u) ? (int32_t)raw : (int32_t)raw - 65536;
}

static void set_analog_available(BatteryAnalogData *data, BatteryAnalogField field,
                                 uint8_t available) {
    if(available) {
        data->available_mask |= (uint32_t)1u << field;
    }
}

static int32_t temperature_centi_c(const uint8_t *info, uint16_t offset) {
    return read_s16_be(info, offset) * 10 - 27310;
}

static void decode_analog(const uint8_t *info) {
    BatteryAnalogData *data = &g_battery_data.analog;
    uint16_t raw;
    int32_t signed_raw;

    data->available_mask = 0u;

    raw = read_u16_be(info, 0u);
    data->system_voltage_mv = raw;
    set_analog_available(data, BATTERY_ANALOG_TOTAL_VOLTAGE, raw != 0xFFFFu);

    signed_raw = read_s16_be(info, 2u);
    data->system_current_ma = signed_raw * 10;
    set_analog_available(data, BATTERY_ANALOG_TOTAL_CURRENT,
                         read_u16_be(info, 2u) != 0xFFFFu);

    data->system_soc_percent = info[4];
    set_analog_available(data, BATTERY_ANALOG_SOC, info[4] != 0xFFu);

    raw = read_u16_be(info, 5u);
    data->average_cycle_count = raw;
    set_analog_available(data, BATTERY_ANALOG_AVERAGE_CYCLES, raw != 0xFFFFu);

    raw = read_u16_be(info, 7u);
    data->max_cycle_count = raw;
    set_analog_available(data, BATTERY_ANALOG_MAX_CYCLES, raw != 0xFFFFu);

    data->average_soh_percent = info[9];
    set_analog_available(data, BATTERY_ANALOG_AVERAGE_SOH, info[9] != 0xFFu);
    data->min_soh_percent = info[10];
    set_analog_available(data, BATTERY_ANALOG_MIN_SOH, info[10] != 0xFFu);

    raw = read_u16_be(info, 11u);
    data->max_cell_voltage_mv = raw;
    set_analog_available(data, BATTERY_ANALOG_MAX_CELL_VOLTAGE, raw != 0xFFFFu);
    raw = read_u16_be(info, 13u);
    data->max_cell_address = raw;
    set_analog_available(data, BATTERY_ANALOG_MAX_CELL_ADDRESS, raw != 0xFFFFu);

    raw = read_u16_be(info, 15u);
    data->min_cell_voltage_mv = raw;
    set_analog_available(data, BATTERY_ANALOG_MIN_CELL_VOLTAGE, raw != 0xFFFFu);
    raw = read_u16_be(info, 17u);
    data->min_cell_address = raw;
    set_analog_available(data, BATTERY_ANALOG_MIN_CELL_ADDRESS, raw != 0xFFFFu);

    raw = read_u16_be(info, 19u);
    data->average_cell_temperature_centi_c = temperature_centi_c(info, 19u);
    set_analog_available(data, BATTERY_ANALOG_AVERAGE_CELL_TEMPERATURE,
                         raw != 0xFFFFu);
    raw = read_u16_be(info, 21u);
    data->max_cell_temperature_centi_c = temperature_centi_c(info, 21u);
    set_analog_available(data, BATTERY_ANALOG_MAX_CELL_TEMPERATURE,
                         raw != 0xFFFFu);
    raw = read_u16_be(info, 23u);
    data->max_cell_temperature_address = raw;
    set_analog_available(data, BATTERY_ANALOG_MAX_CELL_TEMPERATURE_ADDRESS,
                         raw != 0xFFFFu);

    raw = read_u16_be(info, 25u);
    data->min_cell_temperature_centi_c = temperature_centi_c(info, 25u);
    set_analog_available(data, BATTERY_ANALOG_MIN_CELL_TEMPERATURE,
                         raw != 0xFFFFu);
    raw = read_u16_be(info, 27u);
    data->min_cell_temperature_address = raw;
    set_analog_available(data, BATTERY_ANALOG_MIN_CELL_TEMPERATURE_ADDRESS,
                         raw != 0xFFFFu);

    raw = read_u16_be(info, 29u);
    data->average_mos_temperature_centi_c = temperature_centi_c(info, 29u);
    set_analog_available(data, BATTERY_ANALOG_AVERAGE_MOS_TEMPERATURE,
                         raw != 0xFFFFu);
    raw = read_u16_be(info, 31u);
    data->max_mos_temperature_centi_c = temperature_centi_c(info, 31u);
    set_analog_available(data, BATTERY_ANALOG_MAX_MOS_TEMPERATURE,
                         raw != 0xFFFFu);
    raw = read_u16_be(info, 33u);
    data->max_mos_temperature_address = raw;
    set_analog_available(data, BATTERY_ANALOG_MAX_MOS_TEMPERATURE_ADDRESS,
                         raw != 0xFFFFu);

    raw = read_u16_be(info, 35u);
    data->min_mos_temperature_centi_c = temperature_centi_c(info, 35u);
    set_analog_available(data, BATTERY_ANALOG_MIN_MOS_TEMPERATURE,
                         raw != 0xFFFFu);
    raw = read_u16_be(info, 37u);
    data->min_mos_temperature_address = raw;
    set_analog_available(data, BATTERY_ANALOG_MIN_MOS_TEMPERATURE_ADDRESS,
                         raw != 0xFFFFu);

    raw = read_u16_be(info, 39u);
    data->average_bms_temperature_centi_c = temperature_centi_c(info, 39u);
    set_analog_available(data, BATTERY_ANALOG_AVERAGE_BMS_TEMPERATURE,
                         raw != 0xFFFFu);
    raw = read_u16_be(info, 41u);
    data->max_bms_temperature_centi_c = temperature_centi_c(info, 41u);
    set_analog_available(data, BATTERY_ANALOG_MAX_BMS_TEMPERATURE,
                         raw != 0xFFFFu);
    raw = read_u16_be(info, 43u);
    data->max_bms_temperature_address = raw;
    set_analog_available(data, BATTERY_ANALOG_MAX_BMS_TEMPERATURE_ADDRESS,
                         raw != 0xFFFFu);

    raw = read_u16_be(info, 45u);
    data->min_bms_temperature_centi_c = temperature_centi_c(info, 45u);
    set_analog_available(data, BATTERY_ANALOG_MIN_BMS_TEMPERATURE,
                         raw != 0xFFFFu);
    raw = read_u16_be(info, 47u);
    data->min_bms_temperature_address = raw;
    set_analog_available(data, BATTERY_ANALOG_MIN_BMS_TEMPERATURE_ADDRESS,
                         raw != 0xFFFFu);

    data->updated_at_ms = battery_tick_ms;
}

static void decode_alarms(const uint8_t *info) {
    BatteryAlarmData *data = &g_battery_data.alarms;
    data->available_mask = 0u;
    data->alarm_status_1 = info[0];
    data->alarm_status_2 = info[1];
    data->protection_status_1 = info[2];
    data->protection_status_2 = info[3];
    for(uint8_t i = 0u; i < 4u; i++) {
        if(info[i] != 0xFFu) data->available_mask |= (uint8_t)(1u << i);
    }
    data->updated_at_ms = battery_tick_ms;
}

static void decode_charge_data(const uint8_t *info) {
    BatteryChargeData *data = &g_battery_data.charge;
    uint16_t raw;

    data->available_mask = 0u;
    raw = read_u16_be(info, 0u);
    data->charge_voltage_limit_mv = raw;
    if(raw != 0xFFFFu) data->available_mask |= 1u << 0;

    raw = read_u16_be(info, 2u);
    data->discharge_voltage_limit_mv = raw;
    if(raw != 0xFFFFu) data->available_mask |= 1u << 1;

    raw = read_u16_be(info, 4u);
    data->max_charge_current_deciamp = raw;
    if(raw != 0xFFFFu) data->available_mask |= 1u << 2;

    raw = read_u16_be(info, 6u);
    data->max_discharge_current_deciamp = raw;
    if(raw != 0xFFFFu) data->available_mask |= 1u << 3;

    data->charge_discharge_status = info[8];
    if(info[8] != 0xFFu) data->available_mask |= 1u << 4;
    data->updated_at_ms = battery_tick_ms;
}

static BatteryFrameResult parse_response(const uint8_t *frame, uint16_t frame_length,
                                         uint8_t expected_cid2, uint8_t expected_address,
                                         uint8_t *rtn, uint8_t *info,
                                         uint16_t *info_length) {
    uint32_t value;
    uint32_t received_checksum;
    uint16_t lenid;
    uint16_t checksum_position;
    uint16_t expected_length;
    uint16_t sum = 0u;

    if(frame_length < 18u || frame[0] != BATTERY_SOH ||
       frame[frame_length - 1u] != BATTERY_EOI) {
        return FRAME_INVALID;
    }

    if(!read_hex_value((const volatile uint8_t *)frame, 1u, 2u, &value) ||
       !read_hex_value((const volatile uint8_t *)frame, 3u, 2u, &value) ||
       value != expected_address ||
       !read_hex_value((const volatile uint8_t *)frame, 5u, 2u, &value) ||
       value != BATTERY_CID1 ||
       !read_hex_value((const volatile uint8_t *)frame, 7u, 2u, &value)) {
        return FRAME_INVALID;
    }
    *rtn = (uint8_t)value;

    if(!read_hex_value((const volatile uint8_t *)frame, 9u, 4u, &value)) {
        return FRAME_INVALID;
    }
    lenid = (uint16_t)(value & 0x0FFFu);
    if(((value >> 12) & 0x0Fu) != length_checksum(lenid) ||
       (lenid & 1u) != 0u || lenid > BATTERY_ASCII_HEX_SIZE) {
        return FRAME_INVALID;
    }

    expected_length = (uint16_t)(18u + lenid);
    if(frame_length != expected_length) {
        return FRAME_INVALID;
    }

    checksum_position = (uint16_t)(13u + lenid);
    for(uint16_t i = 1u; i < checksum_position; i++) {
        sum = (uint16_t)(sum + frame[i]);
    }
    if(!read_hex_value((const volatile uint8_t *)frame, checksum_position, 4u,
                       &received_checksum)) {
        return FRAME_INVALID;
    }
    if((uint16_t)(0u - sum) != (uint16_t)received_checksum) {
        return FRAME_BAD_CHECKSUM;
    }

    *info_length = (uint16_t)(lenid / 2u);
    for(uint16_t i = 0u; i < *info_length; i++) {
        uint8_t high;
        uint8_t low;
        if(!parse_hex_digit(frame[13u + i * 2u], &high) ||
           !parse_hex_digit(frame[14u + i * 2u], &low)) {
            return FRAME_INVALID;
        }
        info[i] = (uint8_t)((high << 4) | low);
    }

    if(*rtn == 0u) {
        uint16_t required_length = (expected_cid2 == 0x61u) ? 49u :
                                   (expected_cid2 == 0x62u) ? 4u : 9u;
        if(expected_cid2 == 0x60u) {
            if(*info_length < 33u || info[32] == 0xFFu ||
               *info_length != 33u + 16u * (uint16_t)info[32]) {
                return FRAME_BAD_PAYLOAD;
            }
        } else if(*info_length != required_length) {
            return FRAME_BAD_PAYLOAD;
        }
    }
    return FRAME_OK;
}

static void finish_command(uint32_t now) {
    command_index++;
    retry_count = 0u;
    address_fallback_used = 0u;

    if(command_index >= (sizeof(poll_commands) / sizeof(poll_commands[0]))) {
        command_index = 0u;
        poll_state = POLL_IDLE;
        next_action_ms = now + BATTERY_POLL_INTERVAL_MS;
        return;
    }

    poll_state = POLL_INTERFRAME_GAP;
    next_action_ms = now + BATTERY_INTERFRAME_GAP_MS;
}

static void schedule_retry(uint32_t now) {
    retry_count++;
    poll_state = POLL_INTERFRAME_GAP;
    next_action_ms = now + BATTERY_INTERFRAME_GAP_MS;
}

static void record_frame_error(BatteryFrameResult result) {
    if(result == FRAME_BAD_CHECKSUM) {
        g_battery_data.checksum_error_count++;
        g_battery_data.last_error = BATTERY_ERROR_CHECKSUM;
    } else if(result == FRAME_BAD_PAYLOAD) {
        g_battery_data.frame_error_count++;
        g_battery_data.last_error = BATTERY_ERROR_PAYLOAD;
    } else {
        g_battery_data.frame_error_count++;
        g_battery_data.last_error = BATTERY_ERROR_FRAME;
    }
}

static void handle_response(const uint8_t *frame, uint16_t length, uint32_t now) {
    static uint8_t info[BATTERY_MAX_INFO_BYTES];
    uint16_t info_length = 0u;
    uint8_t rtn = 0u;
    uint8_t address = current_address();
    BatteryFrameResult result = parse_response(frame, length,
                                                poll_commands[command_index],
                                                address, &rtn, info, &info_length);

    if(result != FRAME_OK) {
        record_frame_error(result);
        if(retry_count < BATTERY_RETRY_LIMIT) {
            schedule_retry(now);
        } else {
            finish_command(now);
        }
        return;
    }

    g_battery_data.address = address;
    g_battery_data.address_valid = 1u;
    g_battery_data.last_rtn = rtn;
    if(rtn != 0u) {
        g_battery_data.device_error_count++;
        g_battery_data.last_error = BATTERY_ERROR_DEVICE;
        finish_command(now);
        return;
    }

    g_battery_data.successful_responses++;
    g_battery_data.last_error = BATTERY_ERROR_NONE;
    switch(poll_commands[command_index]) {
        case 0x60u:
            g_battery_data.basic.battery_count = info[32];
            g_battery_data.basic.available = 1u;
            g_battery_data.basic.updated_at_ms = battery_tick_ms;
            break;
        case 0x61u:
            decode_analog(info);
            break;
        case 0x62u:
            decode_alarms(info);
            break;
        case 0x63u:
            decode_charge_data(info);
            break;
        default:
            break;
    }
    finish_command(now);
}

static void start_poll_cycle(uint32_t now) {
    command_index = 0u;
    retry_count = 0u;
    address_candidate = 0u;
    address_fallback_used = 0u;
    poll_state = POLL_INTERFRAME_GAP;
    next_action_ms = now + BATTERY_INTERFRAME_GAP_MS;
    g_battery_data.polling_active = 1u;
}

void Battery_RS485_Init(void) {
    uint32_t pclk1;

    RCC->APB2ENR |= (1u << 0) | (1u << 3) | (1u << 4);
    RCC->APB1ENR |= (1u << 18); /* USART3 */

    /* Reset USART3 so stale status/data registers cannot affect the first frame. */
    RCC->APB1RSTR |= (1u << 18);
    RCC->APB1RSTR &= ~(1u << 18);

    /* PB0: battery poll发送指示 */
    GPIOB->CRL &= ~(0xFu << 0u);
    GPIOB->CRL |= (0x3u << 0u);
    GPIOB->BSRR = 1u << (0u + 16u);

    /* PB3 (SCK) 和 PB4 (MISO) 已在 LCD_GPIO_Init 中配置，这里不再重复 */

    /* PB10/PB11 use USART3 default no-remap pins.
       Do not read or write AFIO->MAPR here: it is not needed and must not be
       repeated after LCD_GPIO_Init() has configured SWJ_CFG. */

    /* PB10 USART3_TX = alternate-function push-pull, PB11 USART3_RX = floating input. */
    GPIOB->CRH &= ~((0xFu << 8u) | (0xFu << 12u));
    GPIOB->CRH |= (0xBu << 8u) | (0x4u << 12u);

    /* PB13 controls RS485 DE/RE direction: high = transmit, low = receive. */
    GPIOB->CRH &= ~(0xFu << 20u);
    GPIOB->CRH |= (0x3u << 20u);
    rs485_set_receive();

    GPIOC->CRL &= ~(0xFu << (5u * 4u));
    GPIOC->CRL |=  (0x3u << (5u * 4u));
    GPIOC->BSRR = 1u << (5u + 16u);

    pclk1 = apb1_clock_hz();
    USART3->CR1 = 0u;
    USART3->CR2 = 0u;
    USART3->CR3 = 0u;
    /* Encode fCK / (baud * 16) as BRR mantissa/fraction (16-bit USARTDIV).
       The previous formula passed USARTDIV*16 directly and produced an invalid baud rate. */
    uint32_t brr_denominator = BATTERY_UART_BAUD_RATE * 16u;
    uint32_t brr_mantissa = pclk1 / brr_denominator;
    uint32_t brr_remainder = pclk1 % brr_denominator;
    uint32_t brr_fraction = ((brr_remainder * 16u) + (brr_denominator / 2u)) /
                            brr_denominator;
    if(brr_fraction >= 16u) {
        brr_fraction = 0u;
        brr_mantissa++;
    }
    USART3->BRR = (brr_mantissa << 4) | brr_fraction;
    USART3->CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE |
                  USART_CR1_RXNEIE;

    NVIC_SetPriority(USART3_IRQn, 3u);
    NVIC_EnableIRQ(USART3_IRQn);

    battery_tick_ms = 0u;
    send_led_pulse_deadline = 0u;
    SysTick->LOAD = (SystemCoreClock / 1000u) - 1u;
    SysTick->VAL = 0u;
    SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_TICKINT_Msk |
                    SysTick_CTRL_ENABLE_Msk;

    g_battery_data.polling_active = 1u;
    g_battery_data.last_error = BATTERY_ERROR_NONE;
    poll_state = POLL_IDLE;
    next_action_ms = battery_tick_ms + 100u;
}

void Battery_RS485_SysTickHandler(void) {
    static uint16_t heartbeat_ms;
    static uint8_t heartbeat_on;
    battery_tick_ms++;
    heartbeat_ms++;

    if(g_battery_data.polling_active && heartbeat_ms >= 250u) {
        heartbeat_ms = 0u;
        heartbeat_on = (uint8_t)!heartbeat_on;
        GPIOC->BSRR = heartbeat_on ? (1u << 5) : (1u << (5u + 16u));
    }
}

uint32_t Battery_RS485_GetTickMs(void) {
    return battery_tick_ms;
}

void USART3_IRQHandler(void) {
    uint32_t status = USART3->SR;
    if((status & USART_SR_RXNE) != 0u) {
        uint8_t value = (uint8_t)USART3->DR;
        rx_last_byte_ms = battery_tick_ms;

        if(value == BATTERY_SOH) {
            rx_frame[0] = value;
            rx_frame_count = 1u;
            rx_receiving = 1u;
            rx_frame_ready = 0u;
        } else if(rx_receiving && !rx_frame_ready) {
            if(rx_frame_count < BATTERY_MAX_FRAME_BYTES) {
                rx_frame[rx_frame_count++] = value;
                if(value == BATTERY_EOI) {
                    rx_frame_length = rx_frame_count;
                    rx_frame_ready = 1u;
                    rx_receiving = 0u;
                }
            } else {
                rx_frame_count = 0u;
                rx_receiving = 0u;
            }
        }
    } else if((status & (USART_SR_ORE | USART_SR_FE | USART_SR_NE | USART_SR_PE)) != 0u) {
        (void)USART3->DR;
        rx_frame_count = 0u;
        rx_receiving = 0u;
    }
}

void Battery_RS485_PollService(void) {
    static uint8_t frame[BATTERY_MAX_FRAME_BYTES];
    uint16_t frame_length = 0u;
    uint32_t now = battery_tick_ms;

    /* PB0发送指示：每次发送电池协议请求时短亮 */
    if(send_led_pulse_deadline != 0u &&
       time_reached(now, send_led_pulse_deadline)) {
        GPIOB->BSRR = 1u << (0u + 16u);
        send_led_pulse_deadline = 0u;
    }

    if(poll_state == POLL_WAIT_RESPONSE &&
       take_received_frame(frame, &frame_length)) {
        handle_response(frame, frame_length, now);
        return;
    }

    if(poll_state == POLL_WAIT_RESPONSE &&
       time_reached(now, next_action_ms)) {
        if(poll_commands[command_index] == 0x60u && rx_receiving &&
           (uint32_t)(now - rx_last_byte_ms) < BATTERY_RESPONSE_TIMEOUT_MS &&
           (uint32_t)(now - response_started_ms) < BATTERY_LONG_RESPONSE_TIMEOUT_MS) {
            return;
        }
        if(!g_battery_data.address_valid && command_index == 0u &&
           address_candidate == 0u && !address_fallback_used) {
            address_candidate = 1u;
            address_fallback_used = 1u;
            retry_count = 0u;
            poll_state = POLL_INTERFRAME_GAP;
            next_action_ms = now + BATTERY_INTERFRAME_GAP_MS;
        } else if(retry_count < BATTERY_RETRY_LIMIT) {
            g_battery_data.timeout_count++;
            g_battery_data.last_error = BATTERY_ERROR_TIMEOUT;
            schedule_retry(now);
        } else {
            g_battery_data.timeout_count++;
            g_battery_data.last_error = BATTERY_ERROR_TIMEOUT;
            finish_command(now);
        }
        return;
    }

    if(poll_state == POLL_INTERFRAME_GAP &&
       time_reached(now, next_action_ms)) {
        send_request(poll_commands[command_index]);
        return;
    }

    if(poll_state == POLL_IDLE && time_reached(now, next_action_ms)) {
        start_poll_cycle(now);
    }
}
