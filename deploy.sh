#!/bin/bash

# Default values
DEFAULT_USER="yellowpirat"
DEFAULT_PORT="22"
DEFAULT_TARGET_DIR="/home/yellowpirat/yp-can"

# Script usage
usage() {
    echo "Usage: $0 -i IP_ADDRESS [-u USERNAME] [-p PORT] [-d TARGET_DIR]"
    echo
    echo "Options:"
    echo "  -i IP_ADDRESS   Target device IP address (required)"
    echo "  -u USERNAME     SSH username (default: yellowpirat)"
    echo "  -p PORT        SSH port (default: 22)"
    echo "  -d TARGET_DIR  Target directory (default: /home/yellowpirat/yp-can)"
    echo "  -h            Show this help message"
    exit 1
}

# Parse command line arguments
while getopts "i:u:p:d:h" opt; do
    case $opt in
        i) IP_ADDRESS="$OPTARG" ;;
        u) USERNAME="$OPTARG" ;;
        p) PORT="$OPTARG" ;;
        d) TARGET_DIR="$OPTARG" ;;
        h) usage ;;
        ?) usage ;;
    esac
done

# Check if IP address is provided
if [ -z "$IP_ADDRESS" ]; then
    echo "Error: IP address is required"
    usage
fi

# Set default values if not provided
USERNAME=${USERNAME:-$DEFAULT_USER}
PORT=${PORT:-$DEFAULT_PORT}
TARGET_DIR=${TARGET_DIR:-$DEFAULT_TARGET_DIR}

# Colors for output
GREEN='\033[0;32m'
RED='\033[0;31m'
NC='\033[0m' # No Color

# Function to print status messages
print_status() {
    echo -e "${GREEN}[*]${NC} $1"
}

print_error() {
    echo -e "${RED}[!]${NC} $1"
}

# Create directory structure on target
print_status "Creating directory structure on target..."
ssh -p "$PORT" "$USERNAME@$IP_ADDRESS" "mkdir -p $TARGET_DIR/{src,dts,debian}"

# Transfer files
print_status "Transferring files..."

# Transfer debian directory
scp -P "$PORT" -r debian/* "$USERNAME@$IP_ADDRESS:$TARGET_DIR/debian" || {
    print_error "Failed to transfer debian directory"
    exit 1
}

# Transfer DTS files
scp -P "$PORT" dts/*.dts "$USERNAME@$IP_ADDRESS:$TARGET_DIR/dts/" || {
    print_error "Failed to transfer DTS files"
    exit 1
}

# Transfer source files
scp -P "$PORT" src/* "$USERNAME@$IP_ADDRESS:$TARGET_DIR/src/" || {
    print_error "Failed to transfer source files"
    exit 1
}

# Transfer Kbuild
scp -P "$PORT" Kbuild "$USERNAME@$IP_ADDRESS:$TARGET_DIR/" || {
    print_error "Failed to transfer Makefile"
    exit 1
}

# Transfer Makefile
scp -P "$PORT" Makefile "$USERNAME@$IP_ADDRESS:$TARGET_DIR/" || {
    print_error "Failed to transfer Makefile"
    exit 1
}

print_status "All files transferred successfully to $USERNAME@$IP_ADDRESS:$TARGET_DIR"
