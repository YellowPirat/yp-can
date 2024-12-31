# YellowPirat CAN Driver

Linux kernel driver for the YellowPirat CAN core implemented on DE1-SoC FPGA. The driver provides standard SocketCAN interface for up to 6 CAN channels.

## Quick Start

### Building from Source
```bash
# Build module and device tree overlay
make

# Load for development
make dev-install

# Bring up a CAN interface
sudo ip link set can1 up

# Monitor CAN traffic (requires can-utils)
candump any -tA
```

### Installing via Package
```bash
# Build debian package
make package-deb

# Install the package
sudo dpkg -i ../yp-can-dkms_1.0.0-1_all.deb
```

After package installation, add `dtoverlay=yp-can` to `/boot/config.txt` and reboot.

## Development

### Useful Commands
- Build and load module: `make && make dev-install`
- Quick reload: `make reload IFACE=can1`
- Check status: `make status`
- Monitor kernel messages: `dmesg -w`
- FPGA memory inspection: `sudo memtool md 0xff200000+44`

### Debug Flow
```bash
# Full reload cycle
sudo ip link set can1 down || make dev-uninstall && make dev-install && sudo ip link set can1 up && candump any -tA
```

### CAN Interface Management
```bash
# Bring interface up/down
sudo ip link set can1 up
sudo ip link set can1 down

# Remove interface
sudo ip link del can1
```

## Hardware Details

### Memory Map
Each CAN core occupies 4KB of memory:
- can0: 0xff200000
- can1: 0xff201000
- can2: 0xff202000
- can3: 0xff203000
- can4: 0xff204000
- can5: 0xff205000

### Register Layout
See regs.h for detailed register descriptions.

## Build Options

### Main Targets
- `make` - Build module and device tree overlay
- `make clean` - Clean build files
- `make package` - Build Debian package
- `make dkmsinstall` - Install via DKMS
- `make dkmsremove` - Remove DKMS installation

### Development Targets
- `make dev-install` - Load for development
- `make dev-uninstall` - Unload module
- `make reload` - Quick reload module
- `make status` - Check module status

### Options
- `IFACE=can1` - Specify CAN interface for reload/status commands

## License
GPL v2+
