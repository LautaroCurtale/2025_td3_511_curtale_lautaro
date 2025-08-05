#ifndef AS5600_H_
#define AS5600_H_

#include <stdio.h>
#include <stdint.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "hardware/adc.h"
#include "hardware/irq.h"

#define AS5600_ADDRESS 0x36
#define AS5600_DIR_PORT 0 
#define AS5600_DIR_PIN  21

extern uint8_t magnet_status, md, ml, mh;

extern uint8_t buffer_as6500[3];
// Direction polarity (GND = values increase clockwise,
// VDD = values increase counterclockwise)

void init_as5600_dir();
void set_as5600_dir(uint8_t dir);

uint8_t refresh_magnet_status(void);

uint16_t get_angle_position();

#endif
