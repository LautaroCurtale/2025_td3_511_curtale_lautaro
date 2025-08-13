#include "AS5600.h"
#include <math.h>

#define AS5600_RAW_ANGLE_LSB 0x0E
#define AS5600_RAW_ANGLE_MSB 0x0F

uint8_t magnet_status, md, ml, mh, buffer_as6500[3];

//extern i2c_inst_t *i2c_port; // Debe ser inicializado desde el firmware principal

// Lee un byte desde un registro del AS5600
static uint8_t as5600_read_reg(uint8_t reg) {
    uint8_t val;
    i2c_write_blocking(i2c0, AS5600_ADDRESS, &reg, 1, true);
    i2c_read_blocking(i2c0, AS5600_ADDRESS, &val, 1, false);
    return val;
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

// Devuelve el �ngulo absoluto en grados (0� a 360�)
/*
uint16_t get_angle_position() {
    if(!refresh_magnet_status()){
        return 0;
    }
    uint8_t high = as5600_read_reg(AS5600_RAW_ANGLE_MSB) & 0x0F;
    uint8_t low = as5600_read_reg(AS5600_RAW_ANGLE_LSB);
    uint16_t raw_angle = (high << 8) | low;
    return (float) (raw_angle * 360.0f / 4096.0f);
}
*/
uint16_t get_filtered_angle_position() {
    static float filtered_angle = 0.0f;

    if (!refresh_magnet_status()) {
        return filtered_angle;
    }

    uint8_t high = as5600_read_reg(AS5600_RAW_ANGLE_MSB) & 0x0F;
    uint8_t low  = as5600_read_reg(AS5600_RAW_ANGLE_LSB);
    uint16_t raw_angle = (high << 8) | low;

    float angle = raw_angle * 360.0f / 4096.0f;

    float alpha = 0.4;  // Cuanto más bajo, más suave
    filtered_angle += alpha * (angle - filtered_angle);

    return (float) filtered_angle;
}

uint16_t get_angle_position() {
    static float last_valid_angle = 0.0f;
    float angle = get_filtered_angle_position();

    if (fabsf(angle - last_valid_angle) > 0.01f) {  // Ignora cambios < 0.01°
        last_valid_angle = angle;
    }

    return (float) last_valid_angle;
}