#!/bin/bash
# Usage: ./copy-to-ramdisk.sh <file.elf>

set -e

if [ $# -ne 1 ]; then
    echo "Usage: $0 <file.elf>"
    exit 1
fi

FILE=$1
DISK=build/qemu_disk
MOUNTPOINT=/mbp
OFFSET=$((2048*512))

sudo mount -o loop,offset=$OFFSET "$DISK" "$MOUNTPOINT"
sudo cp "$FILE" "$MOUNTPOINT/"
sudo umount "$MOUNTPOINT"
