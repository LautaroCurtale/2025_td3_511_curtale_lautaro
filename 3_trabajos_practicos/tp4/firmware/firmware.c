#include <stdio.h>
#include "pico/stdlib.h"
#include "bmp280.h"
#include "FreeRTOS.h"
#include "lcd.h"
#include "helper.h"
#include "task.h"
#include "semphr.h"

// Eleccion de I2C a usar
#define I2C         i2c0
// Eleccion de GPIO para SDA
#define SDA_GPI1    16
// Eleccion de GPIO para SCL
#define SCL_GPI1    17
// Direccion de 7 bits del adaptador del LCD
#define LCD_ADDR    0x27

// Mutex para las tareas
SemaphoreHandle_t mutex;

// Estructura para los datos del sensor
typedef struct {
    float temperature;
    int32_t pressure;
} sensor_data_t;

// Estructura para los parametros de calibración
struct bmp280_calib_param bmp_params;

// Cola para datos del sensor
QueueHandle_t queue_sensor;

// Tarea de Init
void task_init(void *params) {
    // Inicializo el I2C con un clock de 100 KHz
    i2c_init(I2C, 100000);
    // Habilito la funcion de I2C en los GPIOs
    gpio_set_function(SDA_GPI1, GPIO_FUNC_I2C);
    gpio_set_function(SCL_GPI1, GPIO_FUNC_I2C);
    // Habilito pull-ups
    gpio_pull_up(SDA_GPI1);
    gpio_pull_up(SCL_GPI1);
    // Inicializa el BMP280 usando el I2C0
    bmp280_init(i2c0);
    // Obtiene parámetros de compensación
    bmp280_get_calib_params(&bmp_params);
    // Inicializacion de cola para estructura
    queue_sensor = xQueueCreate(1, sizeof(sensor_data_t));
    // Inicializo mutex
    mutex = xSemaphoreCreateMutex();
    // Inicializo LCD
    lcd_init(I2C, LCD_ADDR);
    // Limpio pantalla
    lcd_clear();
    // Elimino tarea para liberar recursos
    vTaskDelete(NULL);
}

// Tarea que se encarga de la lectura del sensor
void task_read(void *params) {
    sensor_data_t data;
    while (1) {
        // Obtiene valores sin compensar
        int32_t raw_temperature, raw_pressure;
        // Mutex
        xSemaphoreTake(mutex, portMAX_DELAY);
        bmp280_read_raw(&raw_temperature, &raw_pressure);
        xSemaphoreGive(mutex);
        // Obtiene los valores compensados de temperatura y presión
        data.temperature = bmp280_convert_temp(raw_temperature, &bmp_params);
        data.pressure = bmp280_convert_pressure(raw_pressure, raw_temperature, &bmp_params);
        // Envio datos a la cola
        xQueueOverwrite(queue_sensor, &data); // Overwrite para no bloquear si la cola está llena
        // Bloqueo por un segundo
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// Tarea que imprime en el LCD y limpia el semaforo
void task_print(void *params) {
    char linea1[16];
    char linea2[16];
    // Estructura para la cola
    sensor_data_t data = {0};
    lcd_clear();

    while(1) {
        // Espera un nuevo valor de la cola
        if (xQueueReceive(queue_sensor, &data, portMAX_DELAY)) {
        // Mutex para usar el LCD
        xSemaphoreTake(mutex, portMAX_DELAY);
        lcd_clear();
        // Imprimo en el LCD
        sprintf(linea1, "Temp: %.2f C", data.temperature);
        lcd_set_cursor(0, 0);
   		lcd_string(linea1);
        sprintf(linea2, "Pres: %d KPa", data.pressure/1000);
        lcd_set_cursor(1, 0);
   		lcd_string(linea2);
        xSemaphoreGive(mutex);
        }
        // Bloqueo por un segundo
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// Programa Principal
int main(void) {
    stdio_init_all();

    // Creacion de tareas
    xTaskCreate(task_init, "Inicializacion", 2 * configMINIMAL_STACK_SIZE, NULL, 3, NULL);
    xTaskCreate(task_read, "Lectura", 2 * configMINIMAL_STACK_SIZE, NULL, 1, NULL);
    xTaskCreate(task_print, "Display", 2 * configMINIMAL_STACK_SIZE, NULL, 1, NULL);

    // Arranca el sistema operativo
    vTaskStartScheduler();
    while(1);
}