.PHONY: all clean install dkmsinstall dkmsremove package package-deb dtbo reload status help

# Build environment
PWD := $(shell pwd)
KDIR := /lib/modules/$(shell uname -r)/build

# Package information from debian/changelog
PACKAGE_NAME := $(shell grep -Pom1 '.*(?= \(.*\) .*; urgency=.*)' debian/changelog)
PACKAGE_VERSION := $(shell grep -Pom1 '.* \(\K.*(?=\) .*; urgency=.*)' debian/changelog)

# Device tree
DTC ?= dtc
DTBO_FILE := dts/yp-can.dtbo
DT_OVERLAYS_DIR := /sys/kernel/config/device-tree/overlays/yp-can

# Development defaults
IFACE ?= can1

all: modules dtbo

modules:
	$(MAKE) -C $(KDIR) M=$(PWD) $(MAKEFLAGS) modules

dtbo: $(DTBO_FILE)

$(DTBO_FILE): dts/yp-can.dts
	$(DTC) -@ -I dts -O dtb -o $@ $<

clean:
	# Clean standard build files
	$(MAKE) -C $(KDIR) M=$(PWD) clean
	-rm -f $(DTBO_FILE)

	# Clean package files
	-rm -f src/dkms.conf
	-rm -rf debian/.debhelper
	-rm -f debian/*.debhelper
	-rm -f debian/*.debhelper.log
	-rm -f debian/*.substvars
	-rm -f debian/debhelper-build-stamp
	-rm -f debian/files
	-rm -rf debian/$(PACKAGE_NAME)

install:
	$(MAKE) -C $(KDIR) M=$(PWD) $(MAKEFLAGS) modules_install

dkmsinstall:
	# Check and remove existing version
	if ! [ "$(shell dkms status -m $(PACKAGE_NAME) -v $(PACKAGE_VERSION))" = "" ]; then \
		dkms remove $(PACKAGE_NAME)/$(PACKAGE_VERSION) --all; \
	fi

	# Install source files
	rm -rf /usr/src/$(PACKAGE_NAME)-$(PACKAGE_VERSION)
	rsync --recursive --exclude=*.cmd --exclude=*.d --exclude=*.ko \
		--exclude=*.mod --exclude=*.mod.c --exclude=*.o \
		--exclude=modules.order src/ /usr/src/$(PACKAGE_NAME)-$(PACKAGE_VERSION)

	# Install and build via DKMS
	dkms install $(PACKAGE_NAME)/$(PACKAGE_VERSION)

dkmsremove:
	dkms remove $(PACKAGE_NAME)/$(PACKAGE_VERSION) --all
	rm -rf /usr/src/$(PACKAGE_NAME)-$(PACKAGE_VERSION)

# Development targets
dev-install: modules dtbo
	sudo insmod src/yp-can.ko
	sudo mkdir -p $(DT_OVERLAYS_DIR)
	sudo cp $(DTBO_FILE) $(DT_OVERLAYS_DIR)/dtbo

dev-uninstall:
	-sudo ip link set $(IFACE) down 2>/dev/null || true
	-sudo rmmod yp-can 2>/dev/null || true
	-sudo rm -rf $(DT_OVERLAYS_DIR) 2>/dev/null || true

reload: dev-uninstall dev-install
	sudo ip link set $(IFACE) up
	@echo "Driver reloaded and $(IFACE) brought up"
	@echo "Use 'dmesg -w' to monitor kernel messages"
	@echo "Use 'candump $(IFACE) -tA' to monitor CAN traffic"

status:
	@echo "Module status:"
	@lsmod | grep yp-can || echo "Module not loaded"
	@echo "\nInterface status:"
	@ip -d link show $(IFACE) 2>/dev/null || echo "$(IFACE) not present"
	@echo "\nKernel messages:"
	@dmesg | tail -n 5

# Package building
package: package-deb

package-deb:
	debuild --no-sign

boot-install: dtbo
	sudo cp $(DTBO_FILE) /boot/overlays/yp-can.dtbo
	@echo "Overlay installed. Add 'dtoverlay=yp-can' to /boot/config.txt"

help:
	@echo "Build targets:"
	@echo "  make              - Build module and device tree overlay"
	@echo "  make clean        - Clean build and package files"
	@echo "  make install      - Install modules directly (non-DKMS)"
	@echo ""
	@echo "DKMS targets:"
	@echo "  make dkmsinstall  - Install via DKMS"
	@echo "  make dkmsremove   - Remove DKMS installation"
	@echo ""
	@echo "Package targets:"
	@echo "  make package      - Build all packages"
	@echo "  make package-deb  - Build Debian package"
	@echo ""
	@echo "Development targets:"
	@echo "  make dev-install  - Load module and overlay (for development)"
	@echo "  make dev-uninstall- Unload module and overlay"
	@echo "  make reload       - Quick reload module and bring up interface"
	@echo "  make status       - Show module and interface status"
	@echo ""
	@echo "Options:"
	@echo "  IFACE=can1       - Specify CAN interface (default: can1)"
