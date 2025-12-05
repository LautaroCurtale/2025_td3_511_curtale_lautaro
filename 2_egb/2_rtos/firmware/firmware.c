#include <stdio.h>
#include <string.h>
#include <math.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"
#include "hardware/uart.h"

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
#define IN1 0
#define IN2 1
#define ENA 2
#define LED_VERDE 21
#define LED_ROJO 22
#define UART_TX_PIN 16                  // Pin TX de UART
#define UART_RX_PIN 17                  // Pin RX de UART

//Direcciones
#define LCD_ADD 0x27
#define RTC_ADD 0x68

//Definiciones UART
#define UART_ID uart0                   // Puerto UART
#define BAUD_RATE 115200                // Velocidad de UART
#define DATA_BITS 8                     // Bits de datos de UART
#define STOP_BITS 1                     // Bits de stop de UART
#define PARITY UART_PARITY_NONE         // Sin paridad
#define BUFF_SIZE 26                    // Maximo numero de caracteres a recibir

typedef enum {                          // Distintos tipos de comandos para UART
    CMD_GET,                            // Comando get
    CMD_SET                             // Comando set
} cmd_tipo_t;

typedef enum {                          // Distintas variables accesibles por UART
    VAR_SPO, 
    VAR_TIP, 
    VAR_PEN, 
    VAR_BDE,
    VAR_ANG, 
    VAR_PWM, 
    VAR_ERR, 
    VAR_ENA
} cmd_variable_t;

typedef struct {                        // Estructura para identificar variable
    const char *cmd;                    // String recibido por UART
    cmd_variable_t var;                 // Variable a asignar
} var_map_t;

// Teclado
const uint FILA_PINS[] = {8, 9, 10, 11};
const uint COL_PINS[]  = {12, 13, 14, 15};

const char teclas[4][4] = {
    {'1', '2', '3', 'A'},
    {'4', '5', '6', 'B'},
    {'7', '8', '9', 'C'},
    {'*', '0', '#', 'D'}
};

#define PWM_FRECUENCIA 10000
#define MIN_PWM 80
#define MUESTREOMS 10


//Estructuras Dispositivos
typedef struct {
    char linea1[16];
    char linea2[16];
} lcd_msg_t;

typedef enum {
    DATALOGGER_SAVE_CONFIG,
    DATALOGGER_SAVE_RESULT,
    DATALOGGER_LOAD_CONFIG,
    DATALOGGER_LOAD_RESULT
} datalogger_cmd_t;

typedef struct {
    datalogger_cmd_t cmd;
    void *data; // configuracion_t* o resultado_t*
    SemaphoreHandle_t done;
} datalogger_msg_t;

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
    void *data_in;    // Datos a enviar
    void *data_out;   // Buffer para leer datos
    SemaphoreHandle_t done; // Semáforo para esperar respuesta
} i2c_guard_t;

// QueueHandlers
QueueHandle_t q_lcd;
QueueHandle_t q_angulo;
QueueHandle_t q_pid;
QueueHandle_t q_datalogger;
QueueHandle_t q_i2c_guard;
QueueHandle_t q_pid_enable;
QueueHandle_t q_uart;
QueueHandle_t q_config;
QueueHandle_t q_init;

//Configuracion base
configuracion_t configuracion_actual = {90.0f, 0.0f, 0, 3.0f};

//Interrupcion de UART, envia los caracteres recibidos
void uart_irq_handler() {
    char c;
    BaseType_t to_higher_priority_task = pdFALSE;
    c = uart_getc(UART_ID);
    xQueueSendToBackFromISR(q_uart, &c, &to_higher_priority_task);
    portYIELD_FROM_ISR(to_higher_priority_task);
}

// Prototipo Funciones
void set_motor_pwm(uint8_t gpio);
void motor_sentido_antihorario();
void motor_sentido_horario();
void motor_detenido();

//Tareas

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
                    *ang = (float)get_angle_position(i2c0);
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
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

//Task PID
void task_control_pid(void *params) {
    float kp = 4.0f;
    float ki = 0.01f;
    float kd = 0.1f;
    float Ts = MUESTREOMS / 1000.0f;

    float error_prev = 0.0f;
    float integral   = 0.0f;
    bool pid_enable  = false;
    float ref = 0.0f, ang = 0, salida = 0;
    float coef_velocidad = 0;
    float vel_max = 5.0f;
    float vel_min = 0.18f;
    float error_aceptable = 1.0f;

    while (1) {
        // Lee el valor sin eliminarlo, y sin esperar
        xQueuePeek(q_config, &configuracion_actual, 0);
        bool cmd;
        if (xQueuePeek(q_pid_enable, &cmd, 0)) pid_enable = cmd;

        if (!pid_enable) {
            pwm_set_gpio_level(ENA, 0);
            vTaskDelay(pdMS_TO_TICKS(MUESTREOMS));
            continue;
        }

        xQueuePeek(q_angulo, &ang, 0);

        if (configuracion_actual.tipo_entrada == 0) {
            ref = configuracion_actual.setpoint;
        } else {
            coef_velocidad = configuracion_actual.pendiente;
            ref += vel_min + (coef_velocidad - 1.0f) * (vel_max - vel_min) / 99.0f;
            if (ref > configuracion_actual.setpoint) ref = configuracion_actual.setpoint;
        }

        float error = ref - ang;
        if (error > 180.0f) error -= 360.0f;
        else if (error < -180.0f) error += 360.0f;

        integral += error * Ts;
        if (integral > 500) integral = 500;
        if (integral < -500) integral = -500;

        float derivada = (error - error_prev) / Ts;
        salida = (kp * error) + (ki * integral) + (kd * derivada);
        error_prev = error;

        salida = fmaxf(fminf(salida, 1000), -1000);

        if (fabs(error) > error_aceptable) {
            if (fabs(salida) < MIN_PWM) {
                salida = (salida > 0) ? MIN_PWM : -MIN_PWM;
            }
        } else {
            motor_detenido();
            salida = 0;
            integral = 0; // evita arrastre
        }

        if (salida > 0) {
            motor_sentido_horario();
            pwm_set_gpio_level(ENA, (uint16_t) salida);
        } else if (salida < 0) {
            motor_sentido_antihorario();
            pwm_set_gpio_level(ENA, (uint16_t)(-salida));
        }

        xQueueOverwrite(q_pid, &salida);
        vTaskDelay(pdMS_TO_TICKS(MUESTREOMS));
    }
}


// Task: Flags (LEDs)
void task_flags(void *params) {
    while (1) {
        // Lee el valor sin eliminarlo, y sin esperar
        xQueuePeek(q_config, &configuracion_actual, 0);
        float ang;
        float banda = configuracion_actual.banda_error;
        xQueuePeek(q_angulo, &ang, 0);
        float error = fabsf(fmodf((configuracion_actual.setpoint - ang + 180.0f), 360.0f) - 180.0f);
        bool ok = (error <= banda);
        gpio_put(LED_VERDE, ok);
        gpio_put(LED_ROJO, !ok);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// Task: Datalogger
void task_datalogger(void *params) {
    datalogger_msg_t reqdl;

    while (1) {
        if (xQueueReceive(q_datalogger, &reqdl, portMAX_DELAY)) {
            // Operaciones EEPROM según comando
            SemaphoreHandle_t sem = xSemaphoreCreateBinary();
            i2c_guard_t req;

            switch (reqdl.cmd) {
                case DATALOGGER_SAVE_CONFIG:
                    req.dispositivo = I2C_EEPROM_SAVE_CONF;
                    req.data_in = reqdl.data;
                    req.done = sem;
                    break;

                case DATALOGGER_SAVE_RESULT:
                    req.dispositivo = I2C_EEPROM_SAVE_RESULT;
                    req.data_in = reqdl.data;
                    req.done = sem;
                    break;

                case DATALOGGER_LOAD_CONFIG:
                    req.dispositivo = I2C_EEPROM_LOAD_CONF;
                    req.data_out = reqdl.data;
                    req.done = sem;
                    break;

                case DATALOGGER_LOAD_RESULT:
                    req.dispositivo = I2C_EEPROM_LOAD_RESULT;
                    req.data_out = reqdl.data;
                    req.done = sem;
                    break;
            }
            xQueueSend(q_i2c_guard, &req, portMAX_DELAY);
            xSemaphoreTake(sem, portMAX_DELAY);
            vSemaphoreDelete(sem);
            // Notificar a quien envió el comando que ya terminamos
            if (reqdl.done) {
                xSemaphoreGive(reqdl.done);
            }
        }
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
    resultado_t res;

    while (1) {
            // Lee la configuración actual de la cola
            xQueuePeek(q_config, &configuracion_actual, 0); 
            
            char tecla = escanear_teclado();
            lcd_msg_t msg;
            
            if (estado_menu == 0) {
                switch (tecla) {
                    case 'A': // Menú: Tipo de Salida
                        snprintf(msg.linea1, 16, "Tipo de Salida:");
                        snprintf(msg.linea2, 16, "1:Esc 2:Ram    ");
                        xQueueSend(q_lcd, &msg, 0);
                        estado_menu = 1;
                        break;
                        
                    case 'B': // Monitor
                        // Entra en estado monitor hasta que se presione una tecla
                        while (1) {
                            // Lee valores dinámicos
                            xQueuePeek(q_angulo, &ang, 0);
                            err = fabsf(fmodf((configuracion_actual.setpoint - ang + 180.0f), 360.0f) - 180.0f);
                            
                            snprintf(msg.linea1, 16, "SP:%3.0f VA:%3.0f  ", configuracion_actual.setpoint, ang);
                            snprintf(msg.linea2, 16, "Err:%3.0f TS:%c   ", err, configuracion_actual.tipo_entrada ? 'R' : 'E');
                            xQueueSend(q_lcd, &msg, 0);
                            
                            char salida = escanear_teclado();
                            if (salida != '\0') {
                                estado_menu = 0; // Vuelve al menú principal
                                break; 
                            }
                        }
                        break;
                        
                    case 'C': // Ver Configuración Actual
                        snprintf(msg.linea1, 16, "Salida:%c       ", configuracion_actual.tipo_entrada ? 'R' : 'E');
                        snprintf(msg.linea2, 16, "SP:%3.0f P:%3.0f    ", configuracion_actual.setpoint, configuracion_actual.pendiente);
                        xQueueSend(q_lcd, &msg, 0);
                        break;
                        
                    case 'D': // Cargar Último Resultado (Datalogger)
                        SemaphoreHandle_t sem_load = xSemaphoreCreateBinary();
                        datalogger_msg_t reqdlload = {
                            .cmd = DATALOGGER_LOAD_RESULT,
                            .data = &res,
                            .done = sem_load
                        };
                        xQueueSend(q_datalogger, &reqdlload, portMAX_DELAY);
                        xSemaphoreTake(sem_load, portMAX_DELAY);
                        vSemaphoreDelete(sem_load);
                        
                        snprintf(msg.linea1, 16, "SP:%3.0f OUT:%3.0f ", res.setpoint, res.salida_control);
                        snprintf(msg.linea2, 16, "Ang:%3.0f F:%02d:%02d", res.angulo, res.fecha.hour, res.fecha.minute);
                        xQueueSend(q_lcd, &msg, 0);
                        break;
                        
                    case '#': // PID Activado (ENABLE)
                        enable = true;
                        xQueueOverwrite(q_pid_enable, &enable);
                        snprintf(msg.linea1, 16, "PID Activado   ");
                        snprintf(msg.linea2, 16, "               ");
                        xQueueSend(q_lcd, &msg, 0);
                        break;
                        
                    case '*': // PID Desactivado y Guardar Resultado (DISABLE)
                        enable = false;
                        xQueueOverwrite(q_pid_enable, &enable);
                        
                        // Capturar datos
                        xQueuePeek(q_angulo, &res.angulo, 0);
                        res.setpoint = configuracion_actual.setpoint;
                        xQueuePeek(q_pid, &res.salida_control, 0);
                        res.tipo_entrada = configuracion_actual.tipo_entrada;
                        res.flag_led = (fabsf(configuracion_actual.setpoint - res.angulo) <= 8.0f) ? 1 : 0;
                        
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
                        
                        // Enviar a datalogger, guarda resultados
                        SemaphoreHandle_t sem_save = xSemaphoreCreateBinary();
                        datalogger_msg_t reqdlsave = {
                            .cmd = DATALOGGER_SAVE_RESULT,
                            .data = &res,
                            .done = sem_save
                        };
                        xQueueSend(q_datalogger, &reqdlsave, portMAX_DELAY);
                        xSemaphoreTake(sem_save, portMAX_DELAY);
                        vSemaphoreDelete(sem_save);
                        
                        // Mensaje LCD
                        snprintf(msg.linea1, 16, "PID Desactivado");
                        snprintf(msg.linea2, 16, "Guarda Result. ");
                        xQueueSend(q_lcd, &msg, 0);
                        break;
                }
            } else if (estado_menu == 1) { // Seleccionar Tipo de Salida
                if (tecla == '1' || tecla == '2') {
                    // Actualiza el tipo de entrada en la global
                    configuracion_actual.tipo_entrada = tecla == '2'; 
                    snprintf(msg.linea1, 16, "Setpoint:      ");
                    snprintf(msg.linea2, 16, "               ");
                    xQueueSend(q_lcd, &msg, 0);
                    estado_menu = (tecla == '1') ? 2 : 3;
                    valorset = 0;
                    digitsp = 0;
                }
            } else if (estado_menu == 2) { // Setpoint (Escalón)
                if (tecla >= '0' && tecla <= '9' && digitsp < 3) {
                    valorset = valorset * 10 + (tecla - '0');
                    digitsp++;
                    snprintf(msg.linea2, 16, "%3.0f            ", valorset);
                    xQueueSend(q_lcd, &msg, 0);
                } else if (tecla == '#') { // Guardar y Salir
                    configuracion_actual.setpoint = fminf(valorset, 360.0f);
                    configuracion_actual.pendiente = 0;
                    
                    // Sobrescribir cola para actualizar PID y UART
                    xQueueOverwrite(q_config, &configuracion_actual); 
                    
                    snprintf(msg.linea1, 16, "Guardado       ");
                    snprintf(msg.linea2, 16, "               ");
                    xQueueSend(q_lcd, &msg, 0);
                    estado_menu = 0;
                } else if (tecla == '*') { // Cancelar
                    snprintf(msg.linea1, 16, "Cancelado      ");
                    snprintf(msg.linea2, 16, "               ");
                    xQueueSend(q_lcd, &msg, 0);
                    estado_menu = 0;
                }
            } else if (estado_menu == 3) { // Setpoint (Rampa)
                if (tecla >= '0' && tecla <= '9' && digitsp < 3) {
                    valorset = valorset * 10 + (tecla - '0');
                    digitsp++;
                    snprintf(msg.linea2, 16, "%3.0f            ", valorset);
                    xQueueSend(q_lcd, &msg, 0);
                } else if (tecla == '#') { // Confirmar Setpoint, pasar a Pendiente
                    snprintf(msg.linea1, 16, "Pendiente:     ");
                    snprintf(msg.linea2, 16, "               ");
                    xQueueSend(q_lcd, &msg, 0);
                    valorpend = 0;
                    digitpend = 0;
                    estado_menu = 4;
                } else if (tecla == '*') { // Cancelar
                    snprintf(msg.linea1, 16, "Cancelado      ");
                    snprintf(msg.linea2, 16, "               ");
                    xQueueSend(q_lcd, &msg, 0);
                    estado_menu = 0;
                }
            } else if (estado_menu == 4) { // Pendiente (Rampa)
                if (tecla >= '0' && tecla <= '9' && digitpend < 3) {
                    valorpend = valorpend * 10 + (tecla - '0');
                    digitpend++;
                    snprintf(msg.linea2, 16, "%3.0f            ", valorpend);
                    xQueueSend(q_lcd, &msg, 0);
                } else if (tecla == '#') { // Guardar y Salir
                    configuracion_actual.setpoint = fminf(valorset, 360.0f);
                    configuracion_actual.pendiente = fminf(valorpend, 100.0f);
                    
                    // Sobrescribir cola para actualizar PID y UART
                    xQueueOverwrite(q_config, &configuracion_actual); 
                    
                    snprintf(msg.linea1, 16, "Guardado       ");
                    snprintf(msg.linea2, 16, "               ");
                    xQueueSend(q_lcd, &msg, 0);
                    estado_menu = 0;
                } else if (tecla == '*') { // Cancelar
                    if (configuracion_actual.pendiente == 0) {
                        configuracion_actual.tipo_entrada = 0;
                    }
                    snprintf(msg.linea1, 16, "Cancelado      ");
                    snprintf(msg.linea2, 16, "               ");
                    xQueueSend(q_lcd, &msg, 0);
                    estado_menu = 0;
                }
            }
        }
}

// Task UART
void task_uart(void *params) {
    char in_buff[BUFF_SIZE], out_buff[BUFF_SIZE]; // Buffers de recepcion y envio
    // Tabla con las variables a verificar (8 elementos)
    const var_map_t var_map[] = { 
        {"spo", VAR_SPO},
        {"tip", VAR_TIP},
        {"pen", VAR_PEN},
        {"bde", VAR_BDE},
        {"ang", VAR_ANG},
        {"pwm", VAR_PWM},
        {"err", VAR_ERR},
        {"ena", VAR_ENA},
    };
    char *tipo, *variable, *dato; // Variables a obtener por UART
    uint8_t i = 0, j, recepcion = 0, error = 0;
    cmd_tipo_t tipo_i;
    cmd_variable_t variable_i;
    // Variables para configurar y obtener datos
    configuracion_t config_uart;
    resultado_t result_uart;

    // Leer la configuración actual al inicio de la tarea (si existe)
    xQueuePeek(q_config, &config_uart, 0); 

    while(1) {
        // Recepcion de comando
        xQueueReceive(q_uart, &in_buff[i], portMAX_DELAY);
        
        // Verificamos condicion de fin de comando
        if(in_buff[i] == '\n') {
            in_buff[i] = '\0';
            i = 0;
            recepcion = 1;
        } else {
            i++;
        }
        
        // Verificamos si se recibio basura (primer caracter)
        if(i == 1 && in_buff[0] != 'g' && in_buff[0] != 's') i = 0;
        
        // Verificamos si ocurrio un error y descartamos el dato
        if(i >= BUFF_SIZE) {
            i = 0;
            error = 1; // Error de overflow
        }
        
        // Procesamiento del dato
        if(recepcion == 1) {
            recepcion = 0; // Limpiamos la flag
            tipo = strtok(in_buff, " ");
            variable = strtok(NULL, " ");
            dato = strtok(NULL, " ");

            
            // Obtener la última configuración y los últimos resultados
            xQueuePeek(q_config, &config_uart, 0); 
            xQueuePeek(q_angulo, &result_uart.angulo, 0); 
            xQueuePeek(q_pid, &result_uart.salida_control, 0);

            // Calcular Error (Necesita Setpoint y Ángulo)
            float error_ang = config_uart.setpoint - result_uart.angulo;
            if (error_ang > 180.0f) error_ang -= 360.0f;
            else if (error_ang < -180.0f) error_ang += 360.0f;
            result_uart.error = fabsf(error_ang);
            
            // Verificamos tipo de comando
            if(!error) {
                if(!strcmp(tipo, "get")) {
                    tipo_i = CMD_GET;
                } else if(!strcmp(tipo, "set")) {
                    tipo_i = CMD_SET;
                } else {
                    error = 2; // Error de comando desconocido
                }
            }
            
            // Verificamos tipo de variable
            if(!error && variable != NULL) {
                for(j = 0; j < 8; j++) {
                    if(!strcmp(variable, var_map[j].cmd)) {
                        variable_i = var_map[j].var;
                        break;
                    }
                }
                if(j == 8) error = 3; // Error de variable desconocida
            } else if (!error) {
                error = 3; // Error si falta la variable
            }

            // Procesamiento GET/SET (Modo Operación)
            if(!error) {
                // Cálculo de error para respuesta GET
                float error_ang = config_uart.setpoint - result_uart.angulo;
                if (error_ang > 180.0f) error_ang -= 360.0f;
                else if (error_ang < -180.0f) error_ang += 360.0f;
                result_uart.error = error_ang;
                
                if(tipo_i == CMD_GET) {
                    switch(variable_i) {
                        case VAR_SPO: sprintf(out_buff, "SP=%3.0f\n", config_uart.setpoint); break;
                        case VAR_TIP: sprintf(out_buff, "TE=%d\n", config_uart.tipo_entrada); break;
                        case VAR_PEN: sprintf(out_buff, "P=%3.0f\n", config_uart.pendiente); break;
                        case VAR_BDE: sprintf(out_buff, "BE=%2.0f\n", config_uart.banda_error); break;
                        // Valores dinámicos (resultados)
                        case VAR_PWM: 
                        // La salida interna va de -1000 a 1000.
                        // Dividimos por 10.0 para obtener el porcentaje (-100.0% a 100.0%)
                        float duty = result_uart.salida_control / 10.0f;
                        sprintf(out_buff, "PWM=%3.1f\n", duty); 
                        break;
                        case VAR_ANG: sprintf(out_buff, "ANG=%3.0f\n", result_uart.angulo); break;
                        case VAR_ERR: sprintf(out_buff, "ERR=%3.0f\n", result_uart.error); break;
                        default: error = 3; break;
                    }
                } else { // CMD_SET
                    if (dato == NULL) {
                        error = 4; // Error de dato faltante
                    } else {
                        switch(variable_i) {
                            case VAR_SPO: sscanf(dato, "%f", &config_uart.setpoint); break;
                            case VAR_PEN: sscanf(dato, "%f", &config_uart.pendiente); break;
                            case VAR_BDE: sscanf(dato, "%f", &config_uart.banda_error); break;
                            case VAR_TIP: sscanf(dato, "%d", &config_uart.tipo_entrada); break;
                            case VAR_ENA:
                                int val_enable;
                                sscanf(dato, "%d", &val_enable);
                                bool cmd_bool = (val_enable > 0);
                                // Escribimos en la cola que controla el PID (misma que usa el teclado)
                                xQueueOverwrite(q_pid_enable, &cmd_bool);
                                break;
                            // No se puede "setear" un ángulo, PWM o error (son salidas)
                            case VAR_ANG:
                            case VAR_PWM:
                            case VAR_ERR:
                                error = 5; // Error: Intento de escribir variable de solo lectura
                                break;
                            default: error = 3; break;
                        }

                        if (!error) {
                            // Si la modificación fue exitosa, actualizamos la cola de configuración
                            xQueueOverwrite(q_config, &config_uart); 
                            strcpy(out_buff, "Set ok\n");
                        }
                    }
                }
                
                if (!error) {
                    uart_puts(UART_ID, out_buff);
                }
            }
        }
        
        // Control de errores
        if(error) {
            sprintf(out_buff, "Error %d\n", error);
            uart_puts(UART_ID, out_buff);
            error = 0;
        }
    }
}


// Task Init
void task_init(void *params) {
    i2c_init(I2C_PORT, 100000);
    gpio_set_function(SDA, GPIO_FUNC_I2C);
    gpio_set_function(SCL, GPIO_FUNC_I2C);
    gpio_pull_up(SDA); 
    gpio_pull_up(SCL);
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
    lcd_string("Bienvenido");
    init_as5600();
    pwm_user_init(ENA, PWM_FRECUENCIA);

    // Inicio uart
    uart_init(UART_ID, BAUD_RATE);
    // Pone los pines con la función de UART
    gpio_set_function(UART_TX_PIN, UART_FUNCSEL_NUM(UART_ID, UART_TX_PIN));
    gpio_set_function(UART_RX_PIN, UART_FUNCSEL_NUM(UART_ID, UART_RX_PIN));
    // Desactivamos el control de flujo por hardware
    uart_set_hw_flow(UART_ID, false, false);
    // Indicamos el formato y activamos la FIFO
    uart_set_format(UART_ID, DATA_BITS, STOP_BITS, PARITY);
    uart_set_fifo_enabled(UART_ID, true);
    q_uart = xQueueCreate(BUFF_SIZE, sizeof(char));

    // Habilitamos interrupciones por recepcion de UART
    irq_set_exclusive_handler(UART0_IRQ, uart_irq_handler);
    irq_set_enabled(UART0_IRQ, true);
    uart_set_irq_enables(UART_ID, true, false);

    q_lcd = xQueueCreate(2, sizeof(lcd_msg_t));
    q_angulo = xQueueCreate(1, sizeof(float));
    q_pid = xQueueCreate(1, sizeof(float));
    q_i2c_guard = xQueueCreate(8, sizeof(i2c_guard_t));
    q_datalogger = xQueueCreate(4, sizeof(datalogger_msg_t));
    q_pid_enable = xQueueCreate(1, sizeof(bool));
    q_config = xQueueCreate(1, sizeof(configuracion_t));

    xQueueOverwrite(q_config, &configuracion_actual);

    vTaskDelete(NULL);
}

int main() {
    stdio_init_all();
    xTaskCreate(task_init,        "Init",        configMINIMAL_STACK_SIZE * 3, NULL, 4, NULL);
    xTaskCreate(task_lcd,         "LCD",         configMINIMAL_STACK_SIZE * 2, NULL, 1, NULL);
    xTaskCreate(task_keyboard,    "Keyboard",    configMINIMAL_STACK_SIZE * 4, NULL, 1, NULL);
    xTaskCreate(task_i2c_guard,   "I2C_Guard",   configMINIMAL_STACK_SIZE * 4, NULL, 2, NULL);
    xTaskCreate(task_encoder,     "Encoder",     configMINIMAL_STACK_SIZE * 2, NULL, 2, NULL);
    xTaskCreate(task_control_pid, "PID",         configMINIMAL_STACK_SIZE * 5, NULL, 3, NULL);
    xTaskCreate(task_flags,       "LEDS",        configMINIMAL_STACK_SIZE * 2, NULL, 2, NULL);
    xTaskCreate(task_datalogger,  "Datalogger",  configMINIMAL_STACK_SIZE * 2, NULL, 2, NULL);

    xTaskCreate(task_uart,        "USART",  configMINIMAL_STACK_SIZE * 8, NULL, 2, NULL);
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

void motor_detenido() {
    gpio_put(IN1, 0);
    gpio_put(IN2, 0);
}