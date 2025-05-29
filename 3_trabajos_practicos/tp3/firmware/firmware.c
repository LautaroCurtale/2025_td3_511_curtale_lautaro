#include <stdio.h>
#include "pico/stdlib.h"
#include "helper.h"
#include "FreeRTOS.h"
#include "lcd.h"
#include "task.h"
#include "semphr.h"

// GPIO a usar como señal
#define PWM_PIN 15

// Maximo valor de cuenta para el semaforo
#define MAX_COUNT   10000

// Eleccion de I2C a usar
#define I2C         i2c0
// Eleccion de GPIO para SDA
#define SDA_GPI1    8
// Eleccion de GPIO para SCL
#define SCL_GPI1    9
// Direccion de 7 bits del adaptador del LCD
#define LCD_ADDR        0x27

// Semaforo counting
SemaphoreHandle_t semphr_counting;

//Callback para interrupcion por GPIO

void irq_callback(uint gpio, uint32_t event_mask) {
    // Incremento la cuenta
    BaseType_t to_higher_priority_task = false;
    xSemaphoreGiveFromISR(semphr_counting, &to_higher_priority_task);
    // Reviso si es necesario el cambio a otra tarea
    portYIELD_FROM_ISR(to_higher_priority_task);
}

// Tarea de Init
void task_init(void *params) {
    // Inicializacion de GPIO
    gpio_init(PWM_PIN);
    gpio_set_dir(PWM_PIN, true);
    // Inicializo el I2C con un clock de 100 KHz
    i2c_init(I2C, 100000);
    // Habilito la funcion de I2C en los GPIOs
    gpio_set_function(SDA_GPI1, GPIO_FUNC_I2C);
    gpio_set_function(SCL_GPI1, GPIO_FUNC_I2C);
    // Habilito pull-ups
    gpio_pull_up(SDA_GPI1);
    gpio_pull_up(SCL_GPI1);
    // Inicializo LCD
    lcd_init(I2C, LCD_ADDR);
    // Limpio pantalla
    lcd_clear();
    // Agrego interrupcion por flanco ascendente
    gpio_set_irq_enabled_with_callback(PWM_PIN, GPIO_IRQ_EDGE_RISE, true, irq_callback);
    // Creo semaforo
    semphr_counting = xSemaphoreCreateCounting(MAX_COUNT, 0);
    // Creo PWM de 10KHz
    pwm_user_init(PWM_PIN, 10000);
    // Elimino tarea para liberar recursos
    vTaskDelete(NULL);
}

// Tarea que imprime en el LCD y limpia el semaforo
void task_print(void *params) {
    char buffer[16];
    lcd_clear();

    while(1) {
        // Imprimo en el LCD y limpio semaforo
        sprintf(buffer, "Frec: %d Hz", uxSemaphoreGetCount(semphr_counting));
        lcd_clear();
        lcd_set_cursor(0, 0);
   		lcd_string(buffer);
        xQueueReset(semphr_counting);
        // Bloqueo por un segundo para contar
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// Programa Principal
int main(void) {
    stdio_init_all();

    // Creacion de tareas
    xTaskCreate(task_init, "Inicializacion", configMINIMAL_STACK_SIZE, NULL, 3, NULL);
    xTaskCreate(task_print, "Display", 2 * configMINIMAL_STACK_SIZE, NULL, 2, NULL);

    // Arranca el sistema operativo
    vTaskStartScheduler();
    while(1);
}