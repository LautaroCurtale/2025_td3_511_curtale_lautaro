// Nombre: kernel_module.c
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/serdev.h>
#include <linux/fs.h>
#include <linux/of_device.h>
#include <linux/of.h>
#include <linux/uaccess.h>
#include <linux/wait.h>

// MODIFICADO: Nuevos nombres para el nuevo proyecto
#define AUTHOR              "Curtale-Torreblanca"
#define CHRDEV_NAME         "egb"
#define CHRDEV_MINOR        1
#define CHRDEV_COUNT        1
#define SHARED_BUFFER_SIZE  64
#define BAUD_RATE           115200
#define PARITY              SERDEV_PARITY_NONE

// Variables globales
static dev_t chrdev_number;
static struct cdev chrdev;
static struct class *chrdev_class;
static struct serdev_device *g_serdev = NULL;
static char shared_buffer[SHARED_BUFFER_SIZE];
static int recibido = 0, recibido_size = 0;
static wait_queue_head_t waitqueue;

// ID de compatible para el Device Tree
static struct of_device_id serdev_ids[] = {
    {.compatible = "Curt_Torr,egb", },
    {}
};
MODULE_DEVICE_TABLE(of, serdev_ids);

/*
// Serdev Device
static struct serdev_device *g_serdev = NULL;
// Buffer de datos para compartir entre user y kernel
static char shared_buffer[SHARED_BUFFER_SIZE];
static int recibido = 0, recibido_size = 0;
static wait_queue_head_t waitqueue;
*/

// Prototipos
static ssize_t chr_dev_read(struct file *f, char __user *buff, size_t size, loff_t *off);
static ssize_t chr_dev_write(struct file *f, const char __user *buff, size_t size, loff_t *off);
static int egb_uart_probe(struct serdev_device *serdev);
static void egb_uart_remove(struct serdev_device *serdev);
static size_t egb_uart_recv(struct serdev_device *serdev, const unsigned char *buffer, size_t size);

static struct file_operations chrdev_ops = {
    .owner = THIS_MODULE,
    .read = chr_dev_read,
    .write = chr_dev_write
};

static struct serdev_device_driver pos_uart_driver = {
    .probe = egb_uart_probe,
    .remove = egb_uart_remove,
    .driver = {
        .name = "egb_uart", // Nuevo nombre de driver
        .of_match_table = serdev_ids,
    }
};

static const struct serdev_device_ops pos_uart_ops = {
    .receive_buf = egb_uart_recv,
};

static ssize_t chr_dev_read(struct file *f, char __user *buff, size_t size, loff_t *off) {
    int not_copied;
    if(*off > 0) {
	    return 0;
    }
    wait_event_interruptible(waitqueue, recibido == 1);
    not_copied = copy_to_user(buff, shared_buffer, recibido_size);
    *off = recibido_size - not_copied;
    recibido = 0;
    printk(KERN_INFO "%s: Leido del char device '%s'\n", AUTHOR, shared_buffer);
    return recibido_size - not_copied;
}

static ssize_t chr_dev_write(struct file *f, const char __user *buff, size_t size, loff_t *off) {
    int to_copy, not_copied, len;
    to_copy = min(size, sizeof(shared_buffer) - 1);
    not_copied = copy_from_user(shared_buffer, buff, to_copy);
    len = to_copy - not_copied;
    
    // Imprime en dmesg
    char printk_buff[SHARED_BUFFER_SIZE];
    memcpy(printk_buff, shared_buffer, len);
    printk_buff[len] = '\0';
    if(len > 0 && printk_buff[len - 1] == '\n') printk_buff[len - 1] = '\0';
    printk(KERN_INFO "%s: Escrito sobre /dev/%s - %s\n", AUTHOR, CHRDEV_NAME, printk_buff);

    if(g_serdev != NULL) {
        serdev_device_write_buf(g_serdev, shared_buffer, len);
        return to_copy - not_copied;
    }
    return 0;
}

static int egb_uart_probe(struct serdev_device *serdev) {
    printk(KERN_INFO "%s: Se conecto UART (POS_CTRL)\n", AUTHOR);
    serdev_device_set_client_ops(serdev, &pos_uart_ops);
    if(serdev_device_open(serdev)) {
        printk(KERN_ERR "%s: Error abriendo el UART\n", AUTHOR);
        return -1;
    }
    serdev_device_set_baudrate(serdev, BAUD_RATE);
    serdev_device_set_flow_control(serdev, false);
    serdev_device_set_parity(serdev, PARITY);
    g_serdev = serdev;
    return 0;
}

static void egb_uart_remove(struct serdev_device *serdev) {
    printk(KERN_INFO "%s: UART cerrada (POS_CTRL)\n", AUTHOR);
    serdev_device_close(serdev);
}

static size_t egb_uart_recv(struct serdev_device *serdev, const unsigned char *buffer, size_t size) {
    // Un pequeño filtro de "basura" (mensajes muy cortos)
    if(size > 2) { 
        int to_copy = min(size, SHARED_BUFFER_SIZE - 1);
        memcpy(shared_buffer, buffer, to_copy);
        shared_buffer[to_copy] = '\0';
        recibido_size = to_copy;
        recibido = 1;
        printk(KERN_INFO "%s: Recibido por UART: '%s'\n", AUTHOR, shared_buffer);
        wake_up_interruptible(&waitqueue);
    }
    return size;
}

static int __init module_kernel_init(void) {
    init_waitqueue_head(&waitqueue);
    // ... (Lógica de creación de char device) ...
    if(alloc_chrdev_region(&chrdev_number, CHRDEV_MINOR, CHRDEV_COUNT, AUTHOR) < 0) return -1;
    printk(KERN_INFO "%s: Char device reservado con major %d\n", AUTHOR, MAJOR(chrdev_number));
    
    cdev_init(&chrdev, &chrdev_ops);
    if(cdev_add(&chrdev, chrdev_number, CHRDEV_COUNT) < 0) {
        unregister_chrdev_region(chrdev_number, CHRDEV_COUNT);
        return -1;
    }
    
    chrdev_class = class_create(AUTHOR);
    if(IS_ERR(chrdev_class)) {
        unregister_chrdev_region(chrdev_number, CHRDEV_COUNT);
        printk(KERN_ERR "%s: No se pudo crear el char device\n", AUTHOR);
        return -1;
    }

    // MODIFICADO: Crea /dev/pos_ctrl
    if(IS_ERR(device_create(chrdev_class, NULL, chrdev_number, NULL, CHRDEV_NAME))) {
        class_destroy(chrdev_class);
        unregister_chrdev_region(chrdev_number, CHRDEV_COUNT);
        printk(KERN_ERR "%s: No se pudo crear el char device\n", AUTHOR);
        return -1;
    }
    
    // Registro driver para UART
    if(serdev_device_driver_register(&pos_uart_driver)) {
        printk(KERN_ERR "%s: No se pudo crear el driver de UART\n", AUTHOR);
        return -1;
    }
    printk(KERN_INFO "%s: Char device y driver UART creados\n", AUTHOR);
    return 0;
}

static void __exit module_kernel_exit(void) {
    device_destroy(chrdev_class, chrdev_number);
    class_destroy(chrdev_class);
    unregister_chrdev_region(chrdev_number, CHRDEV_COUNT);
    cdev_del(&chrdev);
    serdev_device_driver_unregister(&pos_uart_driver);
    printk(KERN_INFO "%s: Modulo POS_CTRL removido\n", AUTHOR);
}

module_init(module_kernel_init);
module_exit(module_kernel_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR(AUTHOR);
MODULE_DESCRIPTION("Modulo de kernel para EGB");