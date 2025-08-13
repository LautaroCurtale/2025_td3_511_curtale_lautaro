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
#define ENA 2
#define LED_VERDE 21
#define LED_ROJO 22

#define LCD_ADD 0x27
#define ENC_ADD 0x36
#define RTC_ADD 0x68

// Teclado
const uint FILA_PINS[] = {8, 9, 10, 11};
const uint COL_PINS[]  = {12, 13, 14, 15};

const char teclas[4][4] = {
    {'1', '2', '3', 'A'},
    {'4', '5', '6', 'B'},
    {'7', '8', '9', 'C'},
    {'*', '0', '#', 'D'}
};

#define PWM_FRECUENCIA 2000
#define MUESTREOMS 10

typedef struct {
    char linea1[16];
    char linea2[16];
} lcd_msg_t;

// Globales
QueueHandle_t q_tecla, q_lcd, q_angulo, q_pid, q_flag_led;
SemaphoreHandle_t semaforo_teclado;
configuracion_t configuracion_actual = {90.0f, 0.0f, 0};

// Prototipo Funciones
void set_motor_pwm(uint8_t gpio);
void motor_sentido_antihorario();
void motor_sentido_horario();

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
QueueHandle_t q_pid_enable;


void task_i2c_guard(void *params) {
    i2c_guard_t req;

    while (1) {
        if (xQueueReceive(q_i2c_guard, &req, portMAX_DELAY)) {
            switch (req.dispositivo) {
                case I2C_LCD:
                    lcd_msg_t *msg = (lcd_msg_t*)req.data_in;
                    lcd_set_cursor(0, 0); lcd_string(msg->linea1);
                    lcd_set_cursor(1, 0); lcd_string(msg->linea2);
                    break;
                case I2C_ENCODER:
                    float *ang = (float*)req.data_out;
                    *ang = (float)get_angle_position();  // Usa funciones internas del as5600.c
                    break;
                case I2C_RTC:
                    time_t *t = (time_t*)req.data_out;
                    rtc_get_time(I2C_PORT, t);
                    break;

                case I2C_EEPROM_SAVE_CONF:
                    guardar_configuracion((configuracion_t*)req.data_in);
                    break;

                case I2C_EEPROM_SAVE_RESULT:
                    guardar_resultado((resultado_t*)req.data_in);
                    break;

                case I2C_EEPROM_LOAD_CONF:
                    leer_ultima_configuracion((configuracion_t*)req.data_out);
                    break;

                case I2C_EEPROM_LOAD_RESULT:
                    leer_ultimo_resultado((resultado_t*)req.data_out);
                    break;
            
            }
            if (req.done)
                xSemaphoreGive(req.done);
        }
    }
}

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
        vTaskDelay(pdMS_TO_TICKS(30));
    }
}


// Task: PID
void task_control_pid(void *params) {
    // Parámetros PID
    float kp = 6.0f;  // Ganancia proporcional
    float ki = 0.1f;  // Ganancia integral
    float kd = 0.0f;  // Ganancia derivativa
    float Ts = MUESTREOMS / 1000.0f; // Periodo de muestreo en segundos
    float error_prev = 0.0f;
    float integral = 0.0f;
    int estable_count = 0;
    bool pid_enable = false;
    float ref = 0.0f;
    float ang = 0, salida = 0, salida_prev = 0;

    //set_motor_pwm(ENA);

    while (1) {
        // Chequea si hay señal para habilitar el PID
        bool cmd;
        if (xQueuePeek(q_pid_enable, &cmd, 0)) {
            pid_enable = cmd;
        }

        if (!pid_enable) {
            pwm_set_gpio_level(ENA, 0);
            vTaskDelay(pdMS_TO_TICKS(MUESTREOMS));
            continue;
        }

        //float ang = 0, salida = 0;
        xQueuePeek(q_angulo, &ang, 0);

        if (configuracion_actual.tipo_entrada == 0) {
            ref = configuracion_actual.setpoint;
        } else {
            ref += configuracion_actual.pendiente * 0.02f;
            if (ref > configuracion_actual.setpoint) ref = configuracion_actual.setpoint;
        }

        float error = ref - ang;

        // "wrap" de error a rango [-180, +180] grados
        if (error > 180.0f)
            error -= 360.0f;
        else if (error < -180.0f)
            error += 360.0f;

        // Componente integral (con anti-windup opcional)
        integral += error * Ts;
        
        if(integral >= 100)
        integral = 100;
        if(integral <= -100)
        integral = -100;

        // Componente derivativa
        float derivada = (error - error_prev) / Ts;

        // Salida PID
        salida = (kp * error) + (ki * integral) + (kd * derivada);

        // Fuerza la salida en la banda de error media
        //if (fabsf(error) >= 8.0f && fabsf(error) <= 25.0f)
        //salida = 300.0f * (error > 0 ? 1.0f : -1.0f);

        // Actualizar error previo
        error_prev = error;

        salida = fmaxf(fminf(salida, 500), -500);

        if (salida > 0) {
            motor_sentido_horario();        // CW
            pwm_set_gpio_level(ENA, (uint16_t) salida);
        } else if (salida < 0) {
            motor_sentido_antihorario();    // CCW
            pwm_set_gpio_level(ENA, (uint16_t) -salida);
        }  
        xQueueOverwrite(q_pid, &salida);
        printf("Val=%0.f Sal=%0.f SetPoint:%0.f\n", ang,salida,configuracion_actual.setpoint);
        vTaskDelay(pdMS_TO_TICKS(MUESTREOMS));
    }
}

// Task: Flags (LEDs)
void task_flags(void *params) {
    while (1) {
        float ang;
        xQueuePeek(q_angulo, &ang, 0);
        bool flag = fabsf(configuracion_actual.setpoint - ang) <= 8.0f;
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
        if (xQueuePeek(q_flag_led, &flag, 0));

        if (flag && !ultima_flag && configuracion_actual.setpoint > 0) {
            resultado_t res;
            xQueuePeek(q_angulo, &res.angulo, 0);
            xQueuePeek(q_pid, &res.salida_control, 0);
            res.setpoint = configuracion_actual.setpoint;
            res.flag_led = true;

            SemaphoreHandle_t sem = xSemaphoreCreateBinary();
            i2c_guard_t req_time = {
                .dispositivo = I2C_RTC,
                .data_out = &res.fecha,
                .done = sem
            };
            xQueueSend(q_i2c_guard, &req_time, portMAX_DELAY);
            xSemaphoreTake(sem, portMAX_DELAY);
            vSemaphoreDelete(sem);

            SemaphoreHandle_t sem2 = xSemaphoreCreateBinary();
            i2c_guard_t req_save = {
                .dispositivo = I2C_EEPROM_SAVE_RESULT,
                .data_in = &res,
                .done = sem2
            };
            xQueueSend(q_i2c_guard, &req_save, portMAX_DELAY);
            xSemaphoreTake(sem2, portMAX_DELAY);
            vSemaphoreDelete(sem2);

            guardado = true;
        }

        ultima_flag = flag;
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

// Task: Teclado
void task_keyboard(void *params) {
    static char estado_menu = 0;
    static float valorset = 0.0f;
    static float valorpend = 0.0f;
    static int digitsp = 0;
    static int digitpend = 0;
    bool enable = 0;
    float ang;
    float err;
    bool flag_led;
    resultado_t res;

    while (1) {
            char tecla = escanear_teclado();
            lcd_msg_t msg;
            if (estado_menu == 0) {
                switch (tecla) {
                    case 'A':
                        snprintf(msg.linea1, 16, "Tipo de Salida:");
                        snprintf(msg.linea2, 16, "A:Esc B:Ram    ");
                        xQueueSend(q_lcd, &msg, 0);
                        estado_menu = 1;
                        break;
                    case 'B':
                        // Entrar en modo monitor hasta que se presione '*'
                        while (1) {
                            ang;
                            xQueuePeek(q_angulo, &ang, 0);
                            err = fabs(configuracion_actual.setpoint - ang);
                            snprintf(msg.linea1, 16, "SP:%3.0f VA:%3.0f  ", configuracion_actual.setpoint, ang);
                            snprintf(msg.linea2, 16, "Err:%3.0f TS:%c   ", err, configuracion_actual.tipo_entrada ? 'R' : 'E');
                            xQueueSend(q_lcd, &msg, 0);
                            char salida = escanear_teclado();
                            if (salida != '\0') {
                                // Esperar a que se suelte la tecla antes de salir
                                //while (escanear_teclado() != '\0') {
                                    //vTaskDelay(pdMS_TO_TICKS(50));
                                //}
                            break; // salir del modo monitor
                            }
                            
                        }
                        break;
                    case 'C':
                        snprintf(msg.linea1, 16, "Salida:%c       ", configuracion_actual.tipo_entrada ? 'R' : 'E');
                        snprintf(msg.linea2, 16, "SP:%3.0f P:%3.0f   ", configuracion_actual.setpoint, configuracion_actual.pendiente);
                        xQueueSend(q_lcd, &msg, 0);
                        break;
                    case 'D':
                        SemaphoreHandle_t sem = xSemaphoreCreateBinary();
                        i2c_guard_t req = {
                            .dispositivo = I2C_EEPROM_LOAD_RESULT,
                            .data_out = &res,
                            .done = sem
                        };
                        xQueueSend(q_i2c_guard, &req, portMAX_DELAY);
                        xSemaphoreTake(sem, portMAX_DELAY);
                        vSemaphoreDelete(sem);
                        snprintf(msg.linea1, 16, "SP:%3.0f OUT:%3.0f ", res.setpoint, res.salida_control);
                        snprintf(msg.linea2, 16, "Ang:%3.0f F:%02d:%02d", res.angulo, res.fecha.hour, res.fecha.minute);
                        xQueueSend(q_lcd, &msg, 0);
                        break;
                    case '#': 
                        enable = true;
                        xQueueOverwrite(q_pid_enable, &enable);
                        snprintf(msg.linea1, 16, "PID Activado   ");
                        snprintf(msg.linea2, 16, "               ");
                        xQueueSend(q_lcd, &msg, 0);
                        break;
                    case '*': 
                        enable = false;
                        xQueueOverwrite(q_pid_enable, &enable);
                        xQueuePeek(q_angulo, &res.angulo, 0);
                        res.setpoint = configuracion_actual.setpoint;
                        xQueuePeek(q_pid, &res.salida_control, 0);
                        res.tipo_entrada = configuracion_actual.tipo_entrada;
                        if(fabsf(configuracion_actual.setpoint - res.angulo) <= 8.0f)
                        res.flag_led = 1;
                        else res.flag_led = 0;
                        // Obtener hora desde RTC
                        SemaphoreHandle_t sem_time = xSemaphoreCreateBinary();
                        i2c_guard_t req_time_res = {
                            .dispositivo = I2C_RTC,
                            .data_out = &res.fecha,
                            .done = sem_time
                        };
                        xQueueSend(q_i2c_guard, &req_time_res, portMAX_DELAY);
                        xSemaphoreTake(sem_time, portMAX_DELAY);
                        vSemaphoreDelete(sem_time);
                        // Guardar en EEPROM
                        SemaphoreHandle_t sem_save_res = xSemaphoreCreateBinary();
                        i2c_guard_t req_save_res = {
                            .dispositivo = I2C_EEPROM_SAVE_RESULT,
                            .data_in = &res,
                            .done = sem_save_res
                        };
                        xQueueSend(q_i2c_guard, &req_save_res, portMAX_DELAY);
                        xSemaphoreTake(sem_save_res, portMAX_DELAY);
                        vSemaphoreDelete(sem_save_res);
                        // Mensaje LCD
                        snprintf(msg.linea1, 16, "PID Desactivado");
                        snprintf(msg.linea2, 16, "Guarda Result. ");
                        xQueueSend(q_flag_led, &flag_led, 0);
                        xQueueSend(q_lcd, &msg, 0);
                        break;
                }
            } else if (estado_menu == 1) {
                if (tecla == 'A' || tecla == 'B') {
                    configuracion_actual.tipo_entrada = tecla == 'B';
                    snprintf(msg.linea1, 16, "Setpoint:      ");
                    snprintf(msg.linea2, 16, "               ");
                    xQueueSend(q_lcd, &msg, 0);
                    estado_menu = (tecla == 'A') ? 2 : 3;
                    valorset = 0;
                    digitsp = 0;

                }
            } else if (estado_menu == 2) {
                if (tecla >= '0' && tecla <= '9' && digitsp < 3) {
                    valorset = valorset * 10 + (tecla - '0');
                    digitsp++;
                    snprintf(msg.linea2, 16, "%3.0f            ", valorset);
                    xQueueSend(q_lcd, &msg, 0);
                } else if (tecla == '#') {
                    configuracion_actual.setpoint = fminf(valorset, 360.0f);
                    configuracion_actual.pendiente = 0;
                    snprintf(msg.linea1, 16, "Guardado       ");
                    snprintf(msg.linea2, 16, "               ");
                    xQueueSend(q_lcd, &msg, 0);
                    estado_menu = 0;
                } else if (tecla == '*') {
                    snprintf(msg.linea1, 16, "Cancelado      ");
                    snprintf(msg.linea2, 16, "               ");
                    xQueueSend(q_lcd, &msg, 0);
                    estado_menu = 0;
                }
            } else if (estado_menu == 3) {
                if (tecla >= '0' && tecla <= '9' && digitsp < 3) {
                    valorset = valorset * 10 + (tecla - '0');
                    digitsp++;
                    snprintf(msg.linea2, 16, "%3.0f            ", valorset);
                    xQueueSend(q_lcd, &msg, 0);
                } else if (tecla == '#') {
                    if (estado_menu == 3){
                    snprintf(msg.linea1, 16, "Pendiente:     ");
                    snprintf(msg.linea2, 16, "               ");
                    xQueueSend(q_lcd, &msg, 0);
                    valorpend = 0;
                    digitpend = 0;
                    estado_menu = 4;
                    }
                } else if (tecla == '*') {
                    snprintf(msg.linea1, 16, "Cancelado      ");
                    snprintf(msg.linea2, 16, "               ");
                    xQueueSend(q_lcd, &msg, 0);
                    estado_menu = 0;
                }
            } else if (estado_menu == 4) {
                    if (tecla >= '0' && tecla <= '9' && digitpend < 3) {
                    valorpend = valorpend * 10 + (tecla - '0');
                    digitpend++;
                    snprintf(msg.linea2, 16, "%3.0f            ", valorpend);
                    xQueueSend(q_lcd, &msg, 0);
                    } else if (tecla == '#') {
                        configuracion_actual.setpoint = fminf(valorset, 360.0f);
                        configuracion_actual.pendiente = fminf(valorpend, 30.0f);
                        snprintf(msg.linea1, 16, "Guardado       ");
                        snprintf(msg.linea2, 16, "               ");
                        xQueueSend(q_lcd, &msg, 0);
                        estado_menu = 0;
                    } else if (tecla == '*') {
                        if (configuracion_actual.pendiente == 0)
                        configuracion_actual.tipo_entrada = 1;
                        snprintf(msg.linea1, 16, "Cancelado      ");
                        snprintf(msg.linea2, 16, "               ");
                        xQueueSend(q_lcd, &msg, 0);
                        estado_menu = 0;
                }
            }
        }
}

// Task Init
void task_init(void *params) {
    i2c_init(I2C_PORT, 100000);
    gpio_set_function(SDA, GPIO_FUNC_I2C);
    gpio_set_function(SCL, GPIO_FUNC_I2C);
    gpio_pull_up(SDA); gpio_pull_up(SCL);
    gpio_init(DIR); gpio_set_dir(DIR, GPIO_OUT); gpio_put(DIR, 0);
    gpio_init(LED_VERDE); gpio_set_dir(LED_VERDE, GPIO_OUT); gpio_put(LED_VERDE, 1);
    gpio_init(LED_ROJO);  gpio_set_dir(LED_ROJO,  GPIO_OUT); gpio_put(LED_ROJO,  1);
    gpio_init(IN1);  gpio_set_dir(IN1,  GPIO_OUT); gpio_put(IN1,  0);
    gpio_init(IN2);  gpio_set_dir(IN2,  GPIO_OUT); gpio_put(IN2,  0);
    for (int i = 0; i < FIL; i++) {
        gpio_init(FILA_PINS[i]);
        gpio_set_dir(FILA_PINS[i], GPIO_OUT);
        gpio_put(FILA_PINS[i], 1);
    }
    for (int i = 0; i < COL; i++) {
        gpio_init(COL_PINS[i]);
        gpio_set_dir(COL_PINS[i], GPIO_IN);
        gpio_pull_up(COL_PINS[i]);
    }
    lcd_init(I2C_PORT, LCD_ADD);
    lcd_clear();
    pwm_user_init(ENA, PWM_FRECUENCIA);

    q_tecla = xQueueCreate(1, sizeof(char));
    q_lcd = xQueueCreate(2, sizeof(lcd_msg_t));
    q_angulo = xQueueCreate(1, sizeof(float));
    q_pid = xQueueCreate(1, sizeof(float));
    q_flag_led = xQueueCreate(1, sizeof(bool));
    q_i2c_guard = xQueueCreate(8, sizeof(i2c_guard_t));
    q_pid_enable = xQueueCreate(1, sizeof(bool));

    vTaskDelete(NULL);
}

int main() {
    stdio_init_all();

    xTaskCreate(task_init,        "Init",        configMINIMAL_STACK_SIZE * 2, NULL, 5, NULL);
    xTaskCreate(task_lcd,         "LCD",         configMINIMAL_STACK_SIZE * 4, NULL, 2, NULL);
    xTaskCreate(task_keyboard,    "Keyboard",    configMINIMAL_STACK_SIZE * 4, NULL, 2, NULL);
    xTaskCreate(task_i2c_guard,   "I2C_Guard",   configMINIMAL_STACK_SIZE * 6, NULL, 3, NULL);
    xTaskCreate(task_encoder,     "Encoder",     configMINIMAL_STACK_SIZE * 2, NULL, 3, NULL);
    xTaskCreate(task_control_pid, "PID",         configMINIMAL_STACK_SIZE * 4, NULL, 4, NULL);
    xTaskCreate(task_flags,       "Flags",       configMINIMAL_STACK_SIZE * 2, NULL, 3, NULL);
    xTaskCreate(task_datalogger,  "Datalogger",  configMINIMAL_STACK_SIZE * 4, NULL, 1, NULL);
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

void motor_sentido_horario() {
    gpio_put(IN1, 1);
    gpio_put(IN2, 0);
}

void motor_sentido_antihorario() {
    gpio_put(IN1, 0);
    gpio_put(IN2, 1);
}