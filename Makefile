# YellowPirat CAN Driver Makefile

# Module name and source files
obj-m := yp_can.o
yp_can-y := src/main.o \
            src/netdev.o \
            src/hw.o

# Kernel directory
KERNEL_DIR ?= /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

# Device tree stuff
DTC ?= dtc
DTBO_FILE := yellowpirat.dtbo
DT_OVERLAYS_DIR := /sys/kernel/config/device-tree/overlays/yellowpirat

all: modules dtbo

modules:
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) modules

dtbo: $(DTBO_FILE)

$(DTBO_FILE): dts/yellowpirat.dts
	$(DTC) -@ -I dts -O dtb -o $@ $<

clean:
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) clean
	rm -f $(DTBO_FILE)

install: modules dtbo
	sudo insmod yp_can.ko
	sudo mkdir -p $(DT_OVERLAYS_DIR)
	sudo cp $(DTBO_FILE) $(DT_OVERLAYS_DIR)/dtbo

uninstall:
	sudo rmmod yp_can || true
	sudo rmdir $(DT_OVERLAYS_DIR) || true

.PHONY: all modules dtbo clean install uninstall
