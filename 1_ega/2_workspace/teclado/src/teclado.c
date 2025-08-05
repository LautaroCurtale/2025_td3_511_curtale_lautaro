#include "teclado.h"
#include "pico/stdlib.h"

char keypad_obtener_tecla(index_row_col index) {
    for (uint8_t col = index.col_start; col <= index.col_end; col++) {
        // Poner la columna activa (baja)
        gpio_put(COLUMNA_1 + col, 0);

        for (uint8_t row = index.row_start; row <= index.row_end; row++) {
            if (gpio_get(FILA_1 + row) == 0) {
                sleep_ms(50); // debounce
                if (gpio_get(FILA_1 + row) == 0) {
                    gpio_put(COLUMNA_1 + col, 1); // restaurar columna
                    return KEYPAD_MAP[row][col];
                }
            }
        }

        // Restaurar la columna a alto
        gpio_put(COLUMNA_1 + col, 1);
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

int keypad_esperar_numero(int digitos) {
    int numero = 0;
    char nkey;

    for (int i = 0; i < digitos; i++) {
        do {
            nkey = keypad_esperar_tecla();  // Esperar entrada válida
        } while (nkey < '0' || nkey > '9');

        // Esperar hasta que se suelte la tecla
        while (keypad_obtener_tecla((index_row_col){0, 3, 0, 3}) != '\0') {
            sleep_ms(25);
        }

        numero = numero * 10 + (nkey - '0');
    }

    return numero;
}