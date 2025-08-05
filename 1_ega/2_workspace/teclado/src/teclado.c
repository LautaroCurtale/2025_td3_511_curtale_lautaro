#include "teclado.h"

char keypad_obtener_tecla(index_row_col index) {
    for (uint8_t columna = index.col_start; columna <= index.col_end; columna ++) {
        GPIO_PinWrite (GPIO, PUERTO_0, COLUMNA_1 + columna, 0);
        for (uint8_t fila = index.row_start; fila <= index.row_end; fila ++) {
            if (GPIO_PinRead (GPIO, PUERTO_0, FILA_1 + fila) == 0) {
                delay_ms(50);
                if (GPIO_PinRead (GPIO, PUERTO_0, FILA_1 + fila) == 0) {
                    GPIO_PinWrite (GPIO, PUERTO_0, COLUMNA_1 + columna, 1);
                    return KEYPAD_MAP[fila][columna];
                }
            }
        }
        GPIO_PinWrite (GPIO, PUERTO_0, COLUMNA_1 + columna, 1);;
    }
    return '\0';
}

char keypad_esperar_tecla(void) {
    char key;
    do {
        key = keypad_obtener_tecla((index_row_col) {0, 3, 0, 3});
    } while (key == '\0'); // Esperar hasta que se detecte una tecla
	return key;
}

int keypad_esperar_numero(int digitos) {
    int numero = 0;
    char nkey;

    for (int i = 0; i < digitos; i++) {
        do {
            nkey = keypad_esperar_tecla();  // Esperar entrada válida
        } while (nkey < '0' || nkey > '9');  // Validar que sea un dígito
        while(keypad_obtener_tecla((index_row_col) {0, 3, 0, 3}) != '\0')
        	delay_ms(25);
        numero = numero * 10 + (nkey - '0');  // Construir el número
    }

    return numero;
}
