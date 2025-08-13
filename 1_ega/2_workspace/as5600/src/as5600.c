#include "AS5600.h"

#define AS5600_RAW_ANGLE_LSB 0x0E
#define AS5600_RAW_ANGLE_MSB 0x0F

//extern i2c_inst_t *i2c_port; // Debe ser inicializado desde el firmware principal

// Lee un byte desde un registro del AS5600
static uint8_t as5600_read_reg(uint8_t reg) {
    uint8_t val;
    i2c_write_blocking(i2c0, AS5600_ADDRESS, &reg, 1, true);
    i2c_read_blocking(i2c0, AS5600_ADDRESS, &val, 1, false);
    return val;
}

// Devuelve el �ngulo absoluto en grados (0� a 360�)
uint16_t get_angle_position() {
    uint8_t high = as5600_read_reg(AS5600_RAW_ANGLE_MSB) & 0x0F;
    uint8_t low = as5600_read_reg(AS5600_RAW_ANGLE_LSB);
    uint16_t raw_angle = (high << 8) | low;
    return (float) (raw_angle * 360.0f / 4096.0f);
}

void init_as5600_dir() {
    gpio_init(AS5600_DIR_PIN);
    gpio_set_dir(AS5600_DIR_PIN, GPIO_OUT);
}

void set_as5600_dir(uint8_t dir) {
    gpio_put(AS5600_DIR_PIN, dir);
}

uint8_t refresh_magnet_status(void) {
    uint8_t reg = 0x0B;
    uint8_t val;
    i2c_write_blocking(i2c0, AS5600_ADDRESS, &reg, 1, true);
    i2c_read_blocking(i2c0, AS5600_ADDRESS, &val, 1, false);

    magnet_status = (val >> 3) & 0x07;
    mh = magnet_status & 0x01;
    ml = (magnet_status >> 1) & 0x01;
    md = (magnet_status >> 2) & 0x01;

    return (md && !ml && !mh);
}
