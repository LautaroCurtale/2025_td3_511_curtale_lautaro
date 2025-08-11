#include "teclado.h"
#include "pico/stdlib.h"

char keypad_obtener_tecla(index_row_col index) {
    for (uint8_t col = index.col_start; col <= index.col_end; col++) {
        // Poner la columna activa (baja)
        gpio_put(COL_1 + col, 0);

        for (uint8_t row = index.row_start; row <= index.row_end; row++) {
            if (gpio_get(FILA_1 + row) == 0) {
                sleep_ms(200); // debounce
                if (gpio_get(FILA_1 + row) == 0) {
                    gpio_put(COL_1 + col, 1); // restaurar columna
                    return KEYPAD_MAP[row][col];
                }
            }
        }

        // Restaurar la columna a alto
        gpio_put(COL_1 + col, 1);
    }

    return '\0';
}

char keypad_esperar_tecla(void) {
    char key;
    do {
        key = keypad_obtener_tecla((index_row_col){0, 3, 0, 3});
    } while (key == '\0');
    return key;
}

