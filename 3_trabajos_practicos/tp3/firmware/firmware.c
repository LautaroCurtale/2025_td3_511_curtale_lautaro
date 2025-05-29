#include <stdio.h>
#include "pico/stdlib.h"
#include "helper.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

// GPIOs a usar para medir y para el PWM
#define MEDICION_PIN 14
#define PWM_PIN 15

// Maximo valor de cuenta para el semaforo
#define MAX_COUNT   10000

// Semaforo counting
SemaphoreHandle_t semphr_counting;

// Tarea para imprimir y limpiar el semaforo
void task_print(void *params) {

    while(1) {
        // Mando por consola y limpio semaforo
        printf("Frecuencia: %d Hz \n\n", uxSemaphoreGetCount(semphr_counting));
        xQueueReset(semphr_counting);
        // Bloqueo por un segundo para contar
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// Tarea para contar la frecuencia por polling
void task_count(void *params) {
    bool actual = false;
    bool anterior = false;
    // Aseguro que sea consistente el bloqueo
    TickType_t tick = xTaskGetTickCount();

    while(1) {
        anterior = gpio_get(MEDICION_PIN);
        for (int i = 0; i < 1000; i++){
           actual = gpio_get(MEDICION_PIN);
            if((actual != anterior) && (actual == true)){
                xSemaphoreGive(semphr_counting);
            }
            anterior = actual;
        vTaskDelayUntil(&tick, pdMS_TO_TICKS(0.1));
        
        }
    }
}

// Programa Principal
int main(void) {
    stdio_init_all();
    // Inicializacion de GPIO
    gpio_init(MEDICION_PIN);
    gpio_init(PWM_PIN);
    gpio_set_dir(MEDICION_PIN, false);
    gpio_set_dir(PWM_PIN, true);
    // Creo semaforo
    semphr_counting = xSemaphoreCreateCounting(MAX_COUNT, 0);
    // Creo PWM de 10KHz
    pwm_user_init(PWM_PIN, 10000);

    // Creacion de tareas
    xTaskCreate(task_print, "Consola", 2 * configMINIMAL_STACK_SIZE, NULL, 2, NULL);
    xTaskCreate(task_count, "Contador", configMINIMAL_STACK_SIZE, NULL, 1, NULL);

    // Arranca el sistema operativo
    vTaskStartScheduler();
    while(1);
}

