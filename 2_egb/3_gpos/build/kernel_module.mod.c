#include <linux/module.h>
#include <linux/export-internal.h>
#include <linux/compiler.h>

MODULE_INFO(name, KBUILD_MODNAME);

__visible struct module __this_module
__section(".gnu.linkonce.this_module") = {
	.name = KBUILD_MODNAME,
	.init = init_module,
#ifdef CONFIG_MODULE_UNLOAD
	.exit = cleanup_module,
#endif
	.arch = MODULE_ARCH_INIT,
};



static const struct modversion_info ____versions[]
__used __section("__versions") = {
	{ 0xb1ad28e0, "__gnu_mcount_nc" },
	{ 0x92997ed8, "_printk" },
	{ 0x59991cb6, "serdev_device_close" },
	{ 0xefd6cf06, "__aeabi_unwind_cpp_pr0" },
	{ 0x95ab61d3, "serdev_device_open" },
	{ 0x18b03d15, "serdev_device_set_baudrate" },
	{ 0x9cc7efc0, "serdev_device_set_flow_control" },
	{ 0xeb78ceb7, "serdev_device_set_parity" },
	{ 0xddece99e, "__init_waitqueue_head" },
	{ 0xe3ec2f2b, "alloc_chrdev_region" },
	{ 0x5cc14fc0, "cdev_init" },
	{ 0x20c9b71, "cdev_add" },
	{ 0x6091b333, "unregister_chrdev_region" },
	{ 0x226fd288, "class_create" },
	{ 0x26271443, "device_create" },
	{ 0x19675536, "class_destroy" },
	{ 0x62574d87, "__serdev_device_driver_register" },
	{ 0x5e0478c0, "device_destroy" },
	{ 0x4eb34d75, "cdev_del" },
	{ 0xb032fcb6, "driver_unregister" },
	{ 0x9d669763, "memcpy" },
	{ 0xa8f7cb55, "__wake_up" },
	{ 0x800473f, "__cond_resched" },
	{ 0xfe487975, "init_wait_entry" },
	{ 0x1000e51, "schedule" },
	{ 0x93f6bff1, "prepare_to_wait_event" },
	{ 0x963e0acd, "finish_wait" },
	{ 0x51a910c0, "arm_copy_to_user" },
	{ 0xf0fdf6cb, "__stack_chk_fail" },
	{ 0x7682ba4e, "__copy_overflow" },
	{ 0xae353d77, "arm_copy_from_user" },
	{ 0x5f754e5a, "memset" },
	{ 0x57e9993c, "serdev_device_write_buf" },
	{ 0xc95f5627, "module_layout" },
};

MODULE_INFO(depends, "");

MODULE_ALIAS("of:N*T*CCurt_Torr,egb");
MODULE_ALIAS("of:N*T*CCurt_Torr,egbC*");

MODULE_INFO(srcversion, "E85090F61C8B4349C6CB3AE");
