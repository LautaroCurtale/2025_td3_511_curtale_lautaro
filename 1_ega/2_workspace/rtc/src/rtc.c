// rtc.c + EEPROM adaptado para Pico SDK + FreeRTOS
#include "rtc.h"

static i2c_inst_t *rtc_i2c;

static void i2c_write_bytes(uint8_t reg, uint8_t val) {
    uint8_t datos[] = {reg, val};
    i2c_write_blocking(rtc_i2c, RTC_ADD, datos, 2, false);
}

static void i2c_multiple_read(uint8_t reg, uint8_t len, uint8_t *buf) {
    i2c_write_blocking(rtc_i2c, RTC_ADD, &reg, 1, true);
    i2c_read_blocking(rtc_i2c, RTC_ADD, buf, len, false);
}

static uint8_t dec2bcd(uint8_t val) {
    return ((val / 10) << 4) | (val % 10);
}

static uint8_t bcd2dec(uint8_t val) {
    return ((val >> 4) * 10) + (val & 0x0F);
}

void rtc_init(i2c_inst_t *i2c) {
    rtc_i2c = i2c;
    uint8_t seg;
    i2c_multiple_read(RTC_SECONDS, 1, &seg);
    seg &= ~(1 << 7);
    i2c_write_bytes(RTC_SECONDS, seg);
    i2c_write_bytes(RTC_CONTROL, 0x00);
}

void rtc_set_time(time_t time) {
    uint8_t year = time.year - YEAR_BASE;
    uint8_t data[] = {
        RTC_SECONDS,
        dec2bcd(time.second),
        dec2bcd(time.minute),
        dec2bcd(time.hour),
        dec2bcd(time.day),
        dec2bcd(time.date),
        dec2bcd(time.month),
        dec2bcd(year)
    };
    i2c_write_blocking(rtc_i2c, RTC_ADD, data, 8, false);
}

time_t rtc_get_time(void) {
    time_t fecha;
    uint8_t buf[7];
    i2c_multiple_read(RTC_SECONDS, 7, buf);
    fecha.year = bcd2dec(buf[6]) + YEAR_BASE;
    fecha.month = bcd2dec(buf[5]);
    fecha.date = bcd2dec(buf[4]);
    fecha.day = bcd2dec(buf[3]);
    fecha.hour = bcd2dec(buf[2]);
    fecha.minute = bcd2dec(buf[1]);
    fecha.second = bcd2dec(buf[0]);
    return fecha;
}

// EEPROM: escribe bytes en la direcci�n deseada
void eeprom_write_bytes(uint16_t addr, const uint8_t *data, size_t len) {
    uint8_t buffer[len + 2];
    buffer[0] = (uint8_t)(addr >> 8); // MSB
    buffer[1] = (uint8_t)(addr & 0xFF); // LSB
    memcpy(&buffer[2], data, len);
    i2c_write_blocking(rtc_i2c, EEPROM_ADDR, buffer, len + 2, false);
    sleep_ms(5); // tiempo t�pico de escritura EEPROM
}

// EEPROM: lee bytes desde la direcci�n deseada
void eeprom_read_bytes(uint16_t addr, uint8_t *data, size_t len) {
    uint8_t addr_bytes[] = {(uint8_t)(addr >> 8), (uint8_t)(addr & 0xFF)};
    i2c_write_blocking(rtc_i2c, EEPROM_ADDR, addr_bytes, 2, true);
    i2c_read_blocking(rtc_i2c, EEPROM_ADDR, data, len, false);
}

bool guardar_configuracion(const configuracion_t* conf) {
    eeprom_write_bytes(EEPROM_ADDR_CONFIG, (const uint8_t*)conf, sizeof(configuracion_t));
    return true;
}

bool leer_configuracion(configuracion_t* conf) {
    eeprom_read_bytes(EEPROM_ADDR_CONFIG, (uint8_t*)conf, sizeof(configuracion_t));
    return true;
}

bool guardar_resultado(const resultado_t* res) {
    uint8_t idx;
    eeprom_read_bytes(EEPROM_PTR_ADDR, &idx, 1);

    uint16_t addr = EEPROM_ADDR_RESULT + (idx * sizeof(resultado_t));
    eeprom_write_bytes(addr, (const uint8_t*)res, sizeof(resultado_t));

    idx = (idx + 1) % EEPROM_MAX_RESULTS;
    eeprom_write_bytes(EEPROM_PTR_ADDR, &idx, 1);
    return true;
}

bool leer_ultimo_resultado(resultado_t* res) {
    uint8_t idx;
    eeprom_read_bytes(EEPROM_PTR_ADDR, &idx, 1);
    if (idx == 0) idx = EEPROM_MAX_RESULTS;
    idx--;

    uint16_t addr = EEPROM_ADDR_RESULT + (idx * sizeof(resultado_t));
    eeprom_read_bytes(addr, (uint8_t*)res, sizeof(resultado_t));
    return true;
}
