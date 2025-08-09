#include <stdio.h>
#include <string.h>
#include <math.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

#include "lcd.h"
#include "helper.h"
#include "rtc.h"
#include "as5600.h"
#include "teclado.h"

// Pines
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

// Teclado
#define FILA_1 8
#define FILA_2 9
#define FILA_3 10
#define FILA_4 11
#define COL_1  12
#define COL_2  13
#define COL_3  14
#define COL_4  15

#define PWM_FRECUENCIA 10000

typedef struct {
    char linea1[16];
    char linea2[16];
} lcd_msg_t;

// Globales
QueueHandle_t q_tecla, q_lcd, q_angulo, q_pid, q_flag_led;
SemaphoreHandle_t semaforo_teclado;
configuracion_t configuracion_actual = {90.0f, 10.0f, 0};

// Prototipo Funciones
void set_motor_pwm(uint8_t gpio);

// Tabla de teclas
const char teclado[4][4] = {
    {'1','2','3','A'},
    {'4','5','6','B'},
    {'7','8','9','C'},
    {'*','0','#','D'}
};

typedef enum {
    I2C_LCD,
    I2C_ENCODER,
    I2C_RTC,
    I2C_EEPROM_SAVE_CONF,
    I2C_EEPROM_SAVE_RESULT,
    I2C_EEPROM_LOAD_CONF,
    I2C_EEPROM_LOAD_RESULT
} i2c_dev_t;

typedef struct {
    i2c_dev_t dispositivo;
    void *data_in;    // Datos a enviar (si aplica)
    void *data_out;   // Buffer para leer datos (si aplica)
    SemaphoreHandle_t done; // Semáforo para esperar respuesta
} i2c_guard_t;

QueueHandle_t q_i2c_guard;


void task_i2c_guard(void *params) {
    i2c_guard_t req;

    while (1) {
        if (xQueueReceive(q_i2c_guard, &req, portMAX_DELAY)) {
            switch (req.dispositivo) {

                case I2C_LCD: {
                    lcd_msg_t *msg = (lcd_msg_t*)req.data_in;
                    lcd_clear();
                    lcd_set_cursor(0, 0); lcd_string(msg->linea1);
                    lcd_set_cursor(1, 0); lcd_string(msg->linea2);
                    break;
                }

                case I2C_ENCODER: {
                    float *ang = (float*)req.data_out;
                    *ang = (float)get_angle_position();  // Usa funciones internas del as5600.c
                    break;
                }
            }

            if (req.done)
                xSemaphoreGive(req.done);
        }
    }
}
/*
// Task: LCD
void task_lcd(void *params) {
    bool primer = false;
    lcd_msg_t msg;
    while (1) {
        if (xQueueReceive(q_lcd, &msg, 0) == pdFALSE && primer == false){
            primer = true;
            lcd_clear();
            snprintf(msg.linea1, 16, "Bienvenido");
            lcd_set_cursor(0, 0); lcd_string(msg.linea1);
        }
        if (xQueueReceive(q_lcd, &msg, portMAX_DELAY)) {
            lcd_clear();
            lcd_set_cursor(0, 0); lcd_string(msg.linea1);
            lcd_set_cursor(1, 0); lcd_string(msg.linea2);
        }
    }
}
*/
// Task: LCD
void task_lcd(void *params) {
    lcd_msg_t msg;
    while (1) {
        if (xQueueReceive(q_lcd, &msg, portMAX_DELAY)) {
            i2c_guard_t req = {
                .dispositivo = I2C_LCD,
                .data_in = &msg,
                .done = NULL
            };
            xQueueSend(q_i2c_guard, &req, portMAX_DELAY);
        }
    }
}


// Task: Encoder
/*
void task_encoder(void *params) {
    while (1) {
        float ang = (float)get_angle_position();
        xQueueOverwrite(q_angulo, &ang);
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
*/

void task_encoder(void *params) {
    while (1) {
        float ang = 0;
        SemaphoreHandle_t sem = xSemaphoreCreateBinary();
        i2c_guard_t req = {
            .dispositivo = I2C_ENCODER,
            .data_out = &ang,
            .done = sem
        };
        xQueueSend(q_i2c_guard, &req, portMAX_DELAY);
        xSemaphoreTake(sem, portMAX_DELAY);
        vSemaphoreDelete(sem);

        xQueueOverwrite(q_angulo, &ang);
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

/*
// Task: PID
void task_control_pid(void *params) {
    float kp_pi = 1.0f, ki = 0.1f;
    float kp_pd = 1.0f, kd = 0.1f;
    float error_prev = 0.0f;
    float integral = 0.0f;
    int estable_count = 0;
    set_motor_pwm(ENA);

    while (1) {
        float ang = 0, salida = 0;
        xQueuePeek(q_angulo, &ang, 0);

        static float ref = 0;
        if (configuracion_actual.tipo_entrada == 0) {
            ref = configuracion_actual.setpoint;
        } else {
            ref += configuracion_actual.pendiente * 0.02f;
            if (ref > configuracion_actual.setpoint) ref = configuracion_actual.setpoint;
        }

        float error = ref - ang;
        float delta = error - error_prev;

        if (fabs(error) < 2.0f)
            estable_count++;
        else
            estable_count = 0;

        if (estable_count >= 10)
            salida = kp_pd * error + kd * (delta / 0.02f);
        else {
            integral += error * 0.02f;
            salida = kp_pi * error + ki * integral;
        }

        salida = fmaxf(fminf(salida, 100.0f), -100.0f);
        pwm_set_gpio_level(ENA, salida);
        xQueueOverwrite(q_pid, &salida);
        error_prev = error;

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

// Task: Flags (LEDs)
void task_flags(void *params) {
    while (1) {
        float ang;
        xQueuePeek(q_angulo, &ang, 0);
        bool flag = fabsf(configuracion_actual.setpoint - ang) <= 3.0f;
        gpio_put(LED_VERDE, flag);
        gpio_put(LED_ROJO, !flag);
        xQueueOverwrite(q_flag_led, &flag);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// Task: Datalogger
void task_datalogger(void *params) {
    static bool ultima_flag = false;
    static bool guardado = false;

    while (1) {
        bool flag;
        xQueuePeek(q_flag_led, &flag, 0);

        if (flag && !ultima_flag) {
            resultado_t res;
            xQueuePeek(q_angulo, &res.angulo, 0);
            xQueuePeek(q_pid, &res.salida_control, 0);
            res.setpoint = configuracion_actual.setpoint;
            res.flag_led = true;
            res.fecha = rtc_get_time();
            guardar_resultado(&res);
            guardado = true;
        }

        ultima_flag = flag;
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}
*/
// Task: Teclado
void task_keyboard(void *params) {
    static char estado_menu = 0;
    static float valor = 0;
    static int digitos = 0;

    while (1) {
            char tecla = keypad_esperar_tecla();
            lcd_msg_t msg;

            if (estado_menu == 0) {
                switch (tecla) {
                    case 'A':
                        snprintf(msg.linea1, 16, "Tipo de Salida:");
                        snprintf(msg.linea2, 16, "1:Esc 2:Ram");
                        xQueueSend(q_lcd, &msg, 0);
                        estado_menu = 1;
                        break;
                    case 'B': {
    // Entrar en modo monitor hasta que se presione '*'
    while (1) {
        float ang;
        xQueuePeek(q_angulo, &ang, 0);
        float err = configuracion_actual.setpoint - ang;

        snprintf(msg.linea1, 16, "SP:%f VA:%.1f", configuracion_actual.setpoint, ang);
        snprintf(msg.linea2, 16, "Err:%.1f TS:%c", err, configuracion_actual.tipo_entrada ? 'R' : 'E');
        xQueueSend(q_lcd, &msg, 0);

        // Esperar 200 ms (igual que el muestreo del ángulo)
        vTaskDelay(pdMS_TO_TICKS(200));

        // Revisar si se presionó alguna tecla para salir
        char salida = keypad_obtener_tecla((index_row_col){0, 3, 0, 3});
        if (salida != '\0') {
            // Esperar a que se suelte la tecla antes de salir
            while (keypad_obtener_tecla((index_row_col){0, 3, 0, 3}) != '\0') {
                vTaskDelay(pdMS_TO_TICKS(50));
            }
            break; // salir del modo monitor
        }
    }
    break;
}
                    case 'C':
                        snprintf(msg.linea1, 16, "Salida:%c", configuracion_actual.tipo_entrada ? 'R' : 'E');
                        snprintf(msg.linea2, 16, "SP:%.1f P:%.1f", configuracion_actual.setpoint, configuracion_actual.pendiente);
                        xQueueSend(q_lcd, &msg, 0);
                        break;
                }
            } else if (estado_menu == 1) {
                if (tecla == '1' || tecla == '2') {
                    configuracion_actual.tipo_entrada = tecla == '2';
                    snprintf(msg.linea1, 16, tecla == '1' ? "Setpoint:" : "Pendiente:");
                    msg.linea2[0] = 0;
                    xQueueSend(q_lcd, &msg, 0);
                    estado_menu = (tecla == '1') ? 2 : 3;
                    valor = 0;
                    digitos = 0;
                }
            } else if (estado_menu == 2 || estado_menu == 3) {
                if (tecla >= '0' && tecla <= '9' && digitos < 3) {
                    valor = valor * 10 + (tecla - '0');
                    digitos++;
                    snprintf(msg.linea2, 16, "%.0f", valor);
                    xQueueSend(q_lcd, &msg, 0);
                } else if (tecla == '#') {
                    if (estado_menu == 2)
                        configuracion_actual.setpoint = fminf(valor, 360.0f);
                    else
                        configuracion_actual.pendiente = fminf(valor, 30.0f);

                    //configuracion_actual.fecha = rtc_get_time();
                    //guardar_configuracion(&configuracion_actual);
                    snprintf(msg.linea1, 16, "Guardado!");
                    msg.linea2[0] = 0;
                    xQueueSend(q_lcd, &msg, 0);
                    estado_menu = 0;
                } else if (tecla == '*') {
                    snprintf(msg.linea1, 16, "Cancelado");
                    msg.linea2[0] = 0;
                    xQueueSend(q_lcd, &msg, 0);
                    estado_menu = 0;
                }
            }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

// Task Init
void task_init(void *params) {
    i2c_init(I2C_PORT, 100000);
    gpio_set_function(SDA, GPIO_FUNC_I2C);
    gpio_set_function(SCL, GPIO_FUNC_I2C);
    gpio_pull_up(SDA); gpio_pull_up(SCL);
    gpio_init(DIR); gpio_set_dir(DIR, GPIO_OUT); gpio_put(DIR, 0);
    gpio_init(LED_VERDE); gpio_set_dir(LED_VERDE, GPIO_OUT); gpio_put(LED_VERDE, 0);
    gpio_init(LED_ROJO);  gpio_set_dir(LED_ROJO,  GPIO_OUT); gpio_put(LED_ROJO,  0);

    lcd_init(I2C_PORT, LCD_ADD);
    lcd_clear();
    pwm_user_init(ENA, PWM_FRECUENCIA);

    q_tecla = xQueueCreate(1, sizeof(char));
    q_lcd = xQueueCreate(2, sizeof(lcd_msg_t));
    q_angulo = xQueueCreate(1, sizeof(float));
    q_pid = xQueueCreate(1, sizeof(float));
    q_flag_led = xQueueCreate(1, sizeof(bool));
    q_i2c_guard = xQueueCreate(4, sizeof(i2c_guard_t));
    semaforo_teclado = xSemaphoreCreateBinary();

    for (int i = 0; i < 4; i++) {
        gpio_init(COLUMNA_1 + i);
        gpio_set_dir(COLUMNA_1 + i, GPIO_OUT);
        gpio_put(COLUMNA_1 + i, 1);  // columnas inactivas inicialmente

        gpio_init(FILA_1 + i);
        gpio_set_dir(FILA_1 + i, GPIO_IN);
        gpio_pull_up(FILA_1 + i);    // pull-up para detectar cuando bajan
    }

    vTaskDelete(NULL);
}

int main() {
    stdio_init_all();
    xTaskCreate(task_init, "Init", 1024, NULL, 4, NULL);
    xTaskCreate(task_lcd, "LCD", 2048, NULL, 2, NULL);
    xTaskCreate(task_keyboard, "Keyboard", 2048, NULL, 1, NULL);
    xTaskCreate(task_i2c_guard, "I2C_Guard", 2048, NULL, 3, NULL);
    xTaskCreate(task_encoder, "Encoder", 1024, NULL, 2, NULL);
    //xTaskCreate(task_control_pid, "PID", 2048, NULL, 2, NULL);
    //xTaskCreate(task_flags, "Flags", 1024, NULL, 1, NULL);
    //xTaskCreate(task_datalogger, "Datalogger", 2048, NULL, 1, NULL);
    vTaskStartScheduler();
    while (1);
}


void set_motor_pwm(uint8_t gpio) {
    // Asigna función de PWM
    gpio_set_function(gpio, GPIO_FUNC_PWM);
    // Configura frecuencia de PWM e inicializa
    uint32_t slice = pwm_gpio_to_slice_num(gpio);
    pwm_set_clkdiv(slice, frequency_count_khz(CLOCKS_FC0_SRC_VALUE_CLK_SYS) / 1000.0);
    pwm_set_wrap(slice, 1000000 / PWM_FRECUENCIA);
    pwm_set_gpio_level(gpio, 500000 / PWM_FRECUENCIA);
    pwm_set_enabled(slice, true);
}