#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include "gpio_driver.h"

#define AUTHOR "Lautaro Curtale"


// Puntero para primer hilo
static struct task_struct *thread_on;
// Puntero para segundo hilo
static struct task_struct *thread_off;
static uint8_t led_pin = LED_PIN;

// Funcion para el saludo cada 500ms
static int thread_on_f(void *params) {
	uint8_t led = *(uint8_t*) params;
	msleep(500);
	while(!kthread_should_stop()) {
		gpio_set(led);
		// Mensaje cuando corre la tarea
		printk(KERN_INFO "%s: ON\n", AUTHOR);
		// Demora de medio segundo
		msleep(1000);
	}
	return 0;
}

// Funcion para la despedida cada 500 ms
static int thread_off_f(void *params) {
	uint8_t led = *(uint8_t*) params;
	while(!kthread_should_stop()) {
		gpio_clr(led);
		// Mensaje cuando corre la tarea
		printk(KERN_INFO "%s: OFF\n", AUTHOR);
		// Demora de medio segundo
		msleep(1000);
	}
	return 0;
}

// Se llama cuando el modulo se carga en el kernel
static int __init kernel_module_init(void) {

	if(gpio_map() == NULL) {
		printk(KERN_ERR "%s: Error al solicitar memoria virtual\n", AUTHOR);
		return -1;
	}
	gpio_set_dir_output(led_pin);

	// Mensaje para el kernel
	printk(KERN_INFO "%s: Insertando el modulo de kernel\n", AUTHOR);

	// Creacion de tarea 1
	thread_on = kthread_run(
		thread_on_f,
		(void*) &led_pin,
		"thread_on"
	);

	// Verificacion de error
	if(IS_ERR(thread_on)) {
		printk(KERN_ERR "%s: Error al crear el hilo thread_on\n", AUTHOR);
		return -1;
	}

	// Creacion de tarea 2
	thread_off = kthread_run(
		thread_off_f,
		(void*) &led_pin,
		"thread_off"
	);

	// Verificacion de error
	if(IS_ERR(thread_off)) {
		printk(KERN_ERR "%s: Error al crear el hilo thread_off\n", AUTHOR);
		// Eliminamos la tarea anterior
		kthread_stop(thread_on);
		return -1;
	}
	return 0;
}

//Se llama cuando el modulo se quita del kernel
static void __exit kernel_module_exit(void) {
	// Mensaje
	pr_info("%s: Removiendo el modulo del kernel\n", AUTHOR);
	// Eliminamos los hilos creados
	if(thread_on) {
		kthread_stop(thread_on);
	}
	if(thread_off) {
		kthread_stop(thread_off);
	}
}

// Registro la funcion de inicializacion y salida
module_init(kernel_module_init);
module_exit(kernel_module_exit);

// Informacion del modulo
MODULE_LICENSE("GPL");
MODULE_AUTHOR(AUTHOR);
MODULE_DESCRIPTION("UTN FRA Tecnicas Digitales III - TP5: GPOS");