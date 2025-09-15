#include <stdio.h>
#include <string.h>
#include <math.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "hardware/gpio.h"

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include "lcd.h"
#include "helper.h"

// Pines I2C
#define I2C_PORT i2c0
#define SDA 4
#define SCL 5
#define DIR 6
#define IN1 0
#define IN2 1
#define ENA 3
#define LED_VERDE 21
#define LED_ROJO 22

#define LCD_ADD 0x27
#define ENC_ADD 0x36
#define RTC_ADD 0x68
#define EEPROM_ADDR 0x57  // Ajusta según tu módulo
#define EEPROM_SIZE 4096  // Bytes totales (32Kbit = 4KB)


//Teclado Matricial
#define FILA_1 8
#define FILA_2 9
#define FILA_3 10
#define FILA_4 11
#define COL_1  12
#define COL_2  13
#define COL_3  14
#define COL_4  15

#define FRECUENCIA 10000

// Estructuras
typedef struct {
    float setpoint;
    uint8_t signal_type; // 0 = escalón, 1 = rampa
} setpoint_config_t;

typedef struct {
    float angulo;
    float setpoint;
    float salida_control;
    bool flag_max;
    bool flag_min;
} estado_t;

// Estructura para la EEPROM
bool eeprom_write_struct(uint16_t addr, const void* data, size_t size) {
    if (addr + size > EEPROM_SIZE) return false;

    uint8_t buf[size + 2];
    buf[0] = addr >> 8;      // MSB
    buf[1] = addr & 0xFF;    // LSB
    memcpy(&buf[2], data, size);

    int ret = i2c_write_blocking(I2C_PORT, EEPROM_ADDR, buf, sizeof(buf), false);
    sleep_ms(5); // Tiempo de escritura típico
    return ret >= 0;
}

// Variables
volatile uint8_t tecla_presionada = 0;
volatile bool tecla_leida = false;
int filas[3];
int columnas[3];

// Colas y semáforos
QueueHandle_t q_angulo_actual; 
QueueHandle_t q_setpoint;
QueueHandle_t q_salida_pid; 
QueueHandle_t q_flags;
QueueHandle_t q_estado;
SemaphoreHandle_t semaforo_teclado;

// Tabla de teclas
const char teclado[4][4] = {
    {'1','2','3','A'},
    {'4','5','6','B'},
    {'7','8','9','C'},
    {'*','0','#','D'}
};

// ------------ IRQ teclado -----------------

void gpio_irq_handler(uint gpio, uint32_t events) {
    for (int f = 0; f < 4; f++) {
        if (gpio == filas[f]) {
            for (int c = 0; c < 4; c++) {
                gpio_put(columnas[c], 0);
                sleep_us(3);
                if (!gpio_get(filas[f])) {
                    tecla_presionada = teclado[f][c];
                    tecla_leida = true;
                    xSemaphoreGiveFromISR(semaforo_teclado, NULL);
                }
                gpio_put(columnas[c], 1);
            }
        }
    }
}

// ------------ Tareas -----------------------

void task_encoder_as5600(void *params) {
    while (1) {
        // Lectura del ángulo crudo del AS5600
        uint8_t msb, lsb;
        uint8_t reg = 0x0C; // Dirección del registro ANGLE
        uint8_t buf[2];

        // Iniciar lectura I2C del registro ANGLE
        i2c_write_blocking(I2C_PORT, ENC_ADD, &reg, 1, true);
        i2c_read_blocking(I2C_PORT, ENC_ADD, buf, 2, false);

        msb = buf[0];
        lsb = buf[1];
        uint16_t raw = ((msb << 8) | lsb) & 0x0FFF;

        // Convertir a ángulo en grados
        float angulo = (float)raw * 360.0f / 4096.0f;

        // Enviar ángulo a la cola
        xQueueOverwrite(q_angulo_actual, &angulo);

        vTaskDelay(pdMS_TO_TICKS(10)); // 10 ms de espera
    }
}

/*
void task_generar_setpoint(void *params) {
    static float tiempo = 0;
    setpoint_config_t config = {90.0f, 0};

    while (1) {
        float sp = config.setpoint;
        if (config.signal_type == 1) { // rampa
            sp = fmod(tiempo * 30.0f, 360.0f);
        }
        xQueueOverwrite(q_setpoint, &sp);
        tiempo += 0.02f;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

*/

void task_control_pid(void *params) {
    float kp_pi = 1.0f, ki = 0.1f;      // PI para transitorio
    float kp_pd = 1.0f, kd = 0.1f;      // PD para permanente

    float error_prev = 0.0f;
    float integral = 0.0f;
    int estabilidad_contador = 0;
    const int umbral_estable = 10; // 10 ciclos x 20ms = 200 ms
    const float error_umbral = 5.0f;

    while (1) {
        float setpoint = 0.0f, angulo = 0.0f;
        xQueuePeek(q_setpoint, &setpoint, 0);
        xQueuePeek(q_angulo_actual, &angulo, 0);

        float error = setpoint - angulo;
        float delta_error = error - error_prev;

        // Verificar estabilidad
        if (fabsf(error) < error_umbral) {
            estabilidad_contador++;
        } else {
            estabilidad_contador = 0;
        }

        float salida = 0.0f;

        if (estabilidad_contador >= umbral_estable) {
            // ➤ Régimen permanente → usar PD
            salida = kp_pd * error + kd * (delta_error / 0.02f);
        } else {
            // ➤ Régimen transitorio → usar PI
            integral += error * 0.02f;
            salida = kp_pi * error + ki * integral;
        }

        // Saturación [-100, 100]
        salida = fmaxf(fminf(salida, 100.0f), -100.0f);
        set_motor_pwm(salida);

        xQueueOverwrite(q_salida_pid, &salida);
        error_prev = error;

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void task_flags(void *params) {
    while (1) {
        float angulo;
        xQueuePeek(q_angulo_actual, &angulo, 0);
        bool flag_max = angulo > 2.0f;
        bool flag_min = angulo <= 2.0f;
        xQueueOverwrite(q_flags, &(bool[]){flag_max || flag_min});
        if (flag_max == true){
            gpio_put(LED_VERDE, 0);
            gpio_put(LED_ROJO, 1);
        } else
            gpio_put(LED_ROJO, 0);
            gpio_put(LED_VERDE, 1);

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void task_teclado(void *params) {
    while (1) {
        if (xSemaphoreTake(semaforo_teclado, portMAX_DELAY)) {
            // Enviar la tecla presionada a la cola
            xQueueSend(q_tecla, &tecla_presionada, 0);
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void task_lcd(void *params) {
    //Tarea tentativa, falta configurar
    setpoint_config_t config = {90.0f, 0};
    char buffer1 [16];
    char buffer2 [16];
    while (1) {
        if (xQueuePeek(q_tecla, &tecla_presionada, 0)) {
            switch (tecla_presionada) {
                case 'A': lcd_clear();
                sprintf(buffer1, "Rampa o Pulso");
                sprintf(buffer2, "1:R 2:P");
                lcd_set_cursor(0, 0);
   		        lcd_string(buffer1);
                lcd_set_cursor(1, 0);
   		        lcd_string(buffer2);
                break;
                case '2': lcd_clear();
                sprintf(buffer1, "Datos:");
                sprintf(buffer2, "");
                lcd_set_cursor(0, 0);
   		        lcd_string(buffer1);
                lcd_set_cursor(1, 0);
   		        lcd_string(buffer2);
                break;
                case '3': lcd_clear();
                sprintf(buffer1, "Ult Config:");
                sprintf(buffer2, "");
                lcd_set_cursor(0, 0);
   		        lcd_string(buffer1);
                lcd_set_cursor(1, 0);
   		        lcd_string(buffer2); 
                break;
                case '*': 
                break;
                case '#': 
                break;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

void task_datalogger(void *params) {
        static uint16_t direccion = 0;  // Dirección EEPROM
        static float ultimo_angulo = -1.0f;
    while (1) {
        estado_t estado;
        xQueuePeek(q_angulo_actual, &estado.angulo, 0);
        xQueuePeek(q_setpoint, &estado.setpoint, 0);
        xQueuePeek(q_salida_pid, &estado.salida_control, 0);
        bool flag;
        xQueuePeek(q_flags, &flag, 0);
        estado.flag_max = flag;
        estado.flag_min = flag;

        // Guarda si cambió el ángulo
        if (fabsf(estado.angulo - ultimo_angulo) > 0.5f) {
            if (direccion + sizeof(estado_t) > EEPROM_SIZE)
                direccion = 0;  // Circular

            eeprom_write_struct(direccion, &estado, sizeof(estado_t));
            direccion += sizeof(estado_t);
            ultimo_angulo = estado.angulo;
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

// Tarea de Init
void task_init(void *params) {
        // I2C Init
    i2c_init(I2C_PORT, 100000);
    gpio_set_function(SDA, GPIO_FUNC_I2C);
    gpio_set_function(SCL, GPIO_FUNC_I2C);
    gpio_pull_up(SDA);
    gpio_pull_up(SCL);
    gpio_init(DIR);
    gpio_set_dir(DIR, GPIO_OUT);
    gpio_put(DIR, 0);
    // PWM motor
    pwm_user_init(ENA, FRECUENCIA);
    // Init teclado
    for (int i = 0; i < 4; i++) {
        gpio_init(filas[i]);
        gpio_set_dir(filas[i], GPIO_IN);
        gpio_pull_up(filas[i]);
        gpio_set_irq_enabled_with_callback(filas[i], GPIO_IRQ_EDGE_FALL, true, &gpio_irq_handler);
    }
    // Init LEDS
    gpio_init(LED_ROJO);
    gpio_set_dir(LED_ROJO, true);
    gpio_put(LED_ROJO, 0);
    gpio_init(LED_VERDE);
    gpio_set_dir(LED_VERDE, true);
    gpio_put(LED_VERDE, 0);
    for (int i = 0; i < 4; i++) {
        gpio_init(columnas[i]);
        gpio_set_dir(columnas[i], GPIO_OUT);
        gpio_put(columnas[i], 1);
    }
    // Colas
    q_angulo_actual = xQueueCreate(1, sizeof(float));
    q_setpoint = xQueueCreate(1, sizeof(float));
    q_salida_pid = xQueueCreate(1, sizeof(float));
    q_flags = xQueueCreate(1, sizeof(bool));
    q_estado = xQueueCreate(1, sizeof(estado_t));
    // Semáforo
    semaforo_teclado = xSemaphoreCreateBinary();
    lcd_init(I2C_PORT, LCD_ADD);
    lcd_clear();
    // Variables
    setpoint_config_t config = {90.0f, 0};
    // Elimino tarea para liberar recursos
    vTaskDelete(NULL);
}

// ---------------- MAIN ------------------------

int main() {
    stdio_init_all();
    // Tareas
    xTaskCreate(task_init, "Inicialización", configMINIMAL_STACK_SIZE * 1, NULL, 3, NULL);
    xTaskCreate(task_encoder_as5600, "Encoder", configMINIMAL_STACK_SIZE * 1, NULL, 1, NULL);
    // xTaskCreate(task_generar_setpoint, "Setpoint", configMINIMAL_STACK_SIZE * 1, NULL, 2, NULL);
    xTaskCreate(task_control_pid, "PID", configMINIMAL_STACK_SIZE * 2, NULL, 2, NULL);
    xTaskCreate(task_flags, "Flags", configMINIMAL_STACK_SIZE * 1, NULL, 1, NULL);
    xTaskCreate(task_teclado, "Teclado", configMINIMAL_STACK_SIZE * 1, NULL, 1, NULL);
    xTaskCreate(task_lcd, "LCD", configMINIMAL_STACK_SIZE * 2, NULL, 2, NULL);
    xTaskCreate(task_datalogger, "Datalogger", configMINIMAL_STACK_SIZE * 2, NULL, 2, NULL);

    vTaskStartScheduler();
    while (1);
}