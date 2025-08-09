#ifndef TECLADO_H_
#define TECLADO_H_

#include <stdio.h>
#include <stdint.h>
#include "hardware/i2c.h"

// Teclado
#define FILA_1 8
#define FILA_2 9
#define FILA_3 10
#define FILA_4 11
#define COL_1  12
#define COL_2  13
#define COL_3  14
#define COL_4  15
#define KEYPAD_ROWS 4
#define KEYPAD_COLS 4

typedef struct {
    uint8_t row_start;
    uint8_t row_end;
    uint8_t col_start;
    uint8_t col_end;
} index_row_col;

static const char KEYPAD_MAP[KEYPAD_ROWS][KEYPAD_COLS] = {
    {'1', '2', '3', 'A'},
    {'4', '5', '6', 'B'},
    {'7', '8', '9', 'C'},
    {'*', '0', '#', 'D'}
};

char keypad_esperar_tecla(void);
char keypad_obtener_tecla(index_row_col index);
char keypad_leer_tecla_no_bloq(void);
#endif /* DEFINICIONES_H_ */
