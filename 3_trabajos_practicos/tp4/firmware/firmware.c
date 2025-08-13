#include <stdio.h>
#include <math.h>
#include "pico/stdlib.h"
#include "bmp280.h"
#include "FreeRTOS.h"
#include "lcd.h"
#include "hardware/pwm.h"
#include "hardware/i2c.h"
#include "task.h"
#include "semphr.h"

// Eleccion de I2C a usar
#define I2C         i2c0
// Eleccion de GPIO para SDA
#define SDA_GPI1    16
// Eleccion de GPIO para SCL
#define SCL_GPI1    17
// Eleccion de Pin para Boton
#define BTN_PIN    14
// Eleccion de Pin para PWM
#define PWM_PIN   15
// Direccion de 7 bits del adaptador del LCD
#define LCD_ADDR    0x27
// Set Point Temperatura
#define TSP 25.0

// Mutex para las tareas
SemaphoreHandle_t mutex;

// Estructura para los datos crudos del sensor
typedef struct {
    int32_t temperature;
    int32_t pressure;
} sensor_data_raw_t;

// Estructura para los datos del sensor
typedef struct {
    float temperature;
    float pressure;
} sensor_data_t;

// Estructura para los parametros de calibración
struct bmp280_calib_param bmp_params;

// Cola para datos del sensor
QueueHandle_t queue_sensor;
// Cola para datos del sensor crudos
QueueHandle_t queue_sensor_raw;
// Cola para tarea guardiana
QueueHandle_t queue_mensaje;
// Semaforo para cambio pantalla
SemaphoreHandle_t cambio_pantalla;

// Prototipo función
void setup_pwm(uint8_t gpio);

// Interrupción del botón para cambiar la pantalla
void gpio_irq_handler(uint gpio, uint32_t event_mask) {
    // Verifico la interrupcion
    if (event_mask == GPIO_IRQ_EDGE_RISE) {
        BaseType_t to_higher_priority_task;
        // Semaforo para cambiar de pantalla
        xSemaphoreGiveFromISR(cambio_pantalla, &to_higher_priority_task);
        portYIELD_FROM_ISR(to_higher_priority_task);
    }
}


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
    // Inicializo el botón
    gpio_init(BTN_PIN);
    gpio_set_dir(BTN_PIN, GPIO_IN);
    gpio_set_irq_enabled_with_callback(BTN_PIN, GPIO_IRQ_EDGE_RISE, true, gpio_irq_handler);
    // Inicializa el BMP280 usando el I2C0
    bmp280_init(i2c0);
    // Obtiene parámetros de compensación
    bmp280_get_calib_params(&bmp_params);
    // Inicializacion de colas para estructuras
    queue_sensor = xQueueCreate(1, sizeof(sensor_data_t));
    queue_sensor_raw = xQueueCreate(1, sizeof(sensor_data_raw_t));
    queue_mensaje = xQueueCreate(1, sizeof(uint8_t));
    // Inicializacion de semaforo
    cambio_pantalla = xSemaphoreCreateBinary();
    // Inicializo mutex
    mutex = xSemaphoreCreateMutex();
    // Inicializo LCD
    lcd_init(I2C, LCD_ADDR);
    // Limpio pantalla
    lcd_clear();
    // Elimino tarea para liberar recursos
    vTaskDelete(NULL);
}

// Tarea guardiana I2C
void task_i2c(void *params) {
    char linea1[16], linea2[16];
    sensor_data_raw_t sensor_raw;
    sensor_data_t sensor;
    uint8_t msg;

    while(1) {
        xQueueReceive(queue_mensaje, &msg, portMAX_DELAY);

        if(msg == 0) {
            bmp280_read_raw(&sensor_raw.temperature, &sensor_raw.pressure);
            xQueueSendToBack(queue_sensor_raw, &sensor_raw, pdMS_TO_TICKS(1));
        }

        if(msg == 1 || msg == 2) {
            xQueueReceive(queue_sensor, &sensor, portMAX_DELAY);
            if (msg == 1) {
                sprintf(linea1, "Temp: %.2f C", sensor.temperature);
                sprintf(linea2,  "Pres: %d KPa", sensor.pressure/1000);
            }
            else {
                sprintf(linea1, "T: %5.2f C  ", TSP);
                sprintf(linea2, "Error: %5.2f C  ", fabs(TSP - sensor.temperature));
            }
            lcd_clear();
            lcd_set_cursor(0, 0);
            lcd_string(linea1);
            lcd_set_cursor(1, 0);
            lcd_string(linea2);
        }
    }
}

// Tarea que procesa los datos
void task_data(void *params) {
    struct bmp280_calib_param param;
    sensor_data_raw_t sensor_raw;
    sensor_data_t sensor;
    uint8_t duty;

    // Configuración del PWM
    setup_pwm(PWM_PIN);

    while(1) {
        //Obtiene los valores sin compensar
        xQueueReceive(queue_sensor_raw, &sensor_raw, portMAX_DELAY);
        
        //Obtiene los valores compensados de temperatura y presión
        sensor.temperature = bmp280_convert_temp(sensor_raw.temperature, &param);
        sensor.pressure = bmp280_convert_pressure(sensor_raw.pressure, sensor_raw.temperature, &param);

        xQueueSendToBack(queue_sensor, &sensor, portMAX_DELAY);

        duty = (uint8_t)fabs(TSP - sensor.temperature);

        pwm_set_gpio_level(PWM_PIN, duty);
    }
}

// Tarea que pide a la guardiana leer el sensor
void task_bmp280(void *params) {
    uint8_t msg = 0;

    while(1) {
        xQueueSendToBack(queue_mensaje, &msg, portMAX_DELAY);
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

// Tarea que pide a la guardiana actualizar el display
void task_lcd(void *params) {
    uint8_t msg = 1;
    uint32_t slice = pwm_gpio_to_slice_num(PWM_PIN);

    while(1) {
        if(xSemaphoreTake(cambio_pantalla, pdMS_TO_TICKS(500)) == pdTRUE) {
            gpio_set_irq_enabled(BTN_PIN, GPIO_IRQ_EDGE_RISE, false);
            vTaskDelay(pdMS_TO_TICKS(50));
            gpio_set_irq_enabled(BTN_PIN, GPIO_IRQ_EDGE_RISE, true);
            if(msg == 1) {
                msg = 2;
                pwm_set_enabled(slice, true);
            }
            else {
                msg = 1;
                pwm_set_enabled(slice, false);
            }
        }
        xQueueSendToBack(queue_mensaje, &msg, portMAX_DELAY);
    }
}

// Programa Principal
int main(void) {
    stdio_init_all();

    // Creacion de tareas
    xTaskCreate(task_init, "Inicializacion", 2 * configMINIMAL_STACK_SIZE, NULL, 4, NULL);
    xTaskCreate(task_i2c, "I2C Guardian", 2 * configMINIMAL_STACK_SIZE, NULL, 3, NULL);
    xTaskCreate(task_data, "Procesamiento de Datos", 2 * configMINIMAL_STACK_SIZE, NULL, 2, NULL);
    xTaskCreate(task_bmp280, "Sensor", 2 * configMINIMAL_STACK_SIZE, NULL, 1, NULL);
    xTaskCreate(task_lcd, "Display", 2 * configMINIMAL_STACK_SIZE, NULL, 1, NULL);

    // Arranca el sistema operativo
    vTaskStartScheduler();
    while(1);
}

void setup_pwm(uint8_t gpio) {
    // Asigna función de PWM
    gpio_set_function(gpio, GPIO_FUNC_PWM);
    // Configura frecuencia de PWM e inicializa
    uint32_t slice = pwm_gpio_to_slice_num(gpio);
    pwm_set_clkdiv(slice, 140);
    pwm_set_wrap(slice, 1024);
    pwm_set_gpio_level(gpio, 0);
    pwm_set_enabled(slice, true);

}