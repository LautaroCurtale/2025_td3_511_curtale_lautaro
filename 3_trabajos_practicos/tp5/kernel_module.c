#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include "gpio_driver.h"

#define AUTHOR "Lautaro Curtale"


// Puntero para primer hilo
static struct task_struct *thread_hola;
// Puntero para segundo hilo
static struct task_struct *thread_chau;

// Funcion para el saludo cada 500ms
static int thread_hola_f(void *params) {
	while(!kthread_should_stop()) {
		// Mensaje cuando corre la tarea
		printk(KERN_INFO "%s: Hola desde el kernel!\n", AUTHOR);
		// Demora de medio segundo
		msleep(500);
	}
	return 0;
}

// Funcion para la despedida cada 500 ms
static int thread_chau_f(void *params) {
	while(!kthread_should_stop()) {
		// Mensaje cuando corre la tarea
		printk(KERN_INFO "%s: Chau desde el kernel!\n", AUTHOR);
		// Demora de medio segundo
		msleep(500);
	}
	return 0;
}

// Se llama cuando el modulo se carga en el kernel
static int __init kernel_module_init(void) {
	// Mensaje para el kernel
	printk(KERN_INFO "%s: Insertando el modulo de kernel\n", AUTHOR);
	// Creacion de tarea 1
	thread_hola = kthread_run(
		thread_hola_f,
		NULL,
		"thread_hola"
	);
	// Verificacion de error
	if(IS_ERR(thread_hola)) {
		printk(KERN_ERR "%s: Error al crear el hilo thread1_hola\n", AUTHOR);
		return -1;
	}
	// Creacion de tarea 2
	thread_chau = kthread_run(
		thread_chau_f,
		NULL,
		"thread_chau"
	);
	// Verificacion de error
	if(IS_ERR(thread_chau)) {
		printk(KERN_ERR "%s: Error al crear el hilo thread2_chau\n", AUTHOR);
		// Eliminamos la tarea anterior
		kthread_stop(thread_hola);
		return -1;
	}
	return 0;
}

//Se llama cuando el modulo se quita del kernel
static void __exit kernel_module_exit(void) {
	// Mensaje
	pr_info("%s: Removiendo el modulo del kernel\n", AUTHOR);
	// Eliminamos los hilos creados
	if(thread_hola) {
		kthread_stop(thread_hola);
	}
	if(thread_chau) {
		kthread_stop(thread_chau);
	}
}

// Registro la funcion de inicializacion y salida
module_init(kernel_module_init);
module_exit(kernel_module_exit);

// Informacion del modulo
MODULE_LICENSE("GPL");
MODULE_AUTHOR(AUTHOR);
MODULE_DESCRIPTION("UTN FRA Tecnicas Digitales III - TP5: GPOS");
