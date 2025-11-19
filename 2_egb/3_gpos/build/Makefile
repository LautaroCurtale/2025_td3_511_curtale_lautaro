# Etiqueta para definir el nombre del archivo con el codigo del modulo
FILE := kernel_module
DEV_TREE := uart0-egb
# Define el autor del modulo (coincide con el del codigo del modulo)
AUTHOR := Curtale-Torreblanca
# Directorio donde se encuentra el kernel
KDIR := /lib/modules/$(shell uname -r)/build
# Directorio destino de archivos de compilacion
MDIR := $(PWD)/build

# Codigo fuente para compilar
obj-m += ${FILE}.o

# Compilacion completa
all:
	mkdir -p build;					\
	cp -f ${FILE}.c Makefile ${MDIR};	\
	cd build;									\
	make -C ${KDIR} M=${MDIR} modules

# Limpia el directorio de compilacion
clean:
	@cd build;							\
	make -C ${KDIR} M=${MDIR} clean;	\
	cd ..;	\
	rm -rf build

build-devtree:
	@dtc -@ -I dts -O dtb -o build/${DEV_TREE}.dtbo ${DEV_TREE}.dts

load-devtree:
	@sudo cp build/${DEV_TREE}.dtbo /boot/firmware/overlays/
	@sudo dtoverlay ${DEV_TREE}

unload-devtree:
	@sudo dtoverlay -r ${DEV_TREE}

# Carga el modulo en el kernel
insmod:
	@sudo insmod build/${FILE}.ko
	@sudo chmod 666 /dev/egb

# Quita el modulo del kernel
rmmod:
	@sudo rmmod ${FILE}

# Muestra los mensajes del kernel que imprimio este modulo
dmesg:
	@dmesg -T | grep ${AUTHOR}

# Muestra ayuda para el Makefile
help:
	@echo "all: Compila el modulo del kernel desarrollado (por defecto)"
	@echo "clean: Limpia los resultados de la compilacion"
	@echo "build-devtree: Compila el device tree"
	@echo "load-devtree: Carga el device tree"
	@echo "unload-devtree: Remueve el device tree"
	@echo "insmod: Carga el modulo de kernel"
	@echo "rmmod: Remueve el modulo de kernel"
	@echo "dmesg: Muestra mensajes de kernel"
	@echo "help: Muestra esta ayuda"
