#!/bin/bash
# Usage: ./copy-to-ramdisk.sh <file.elf> <partition_number>

set -e

if [ $# -ne 2 ]; then
    echo "Usage: $0 <file.elf> <partition_number>"
    exit 1
fi

FILE=$1
PART=$2
DISK=build/qemu_disk
MOUNTPOINT=$PWD/build/mbp

SECTOR_SIZE=512
POFFSET=2048  # first partition starts at 2048

# Get partition size (FS_COUNT) from fdisk output
# Partition entries look like: build/qemu_disk1   *   2048  ...  <end>  <size>
FS_COUNT=$(fdisk -l $DISK | awk -v d="$DISK" '$1 ~ d {print $4; exit}')

if [ -z "$FS_COUNT" ]; then
    echo "Failed to get partition size from fdisk"
    exit 1
fi

# Compute offset: partition N starts at POFFSET + (N-1)*FS_COUNT
OFFSET=$(( (POFFSET + (PART - 1) * FS_COUNT) * SECTOR_SIZE ))

echo "Mounting partition $PART at offset=$OFFSET"
sudo mount -o loop,offset=$OFFSET "$DISK" "$MOUNTPOINT"
sudo cp "$FILE" "$MOUNTPOINT/"
sudo umount "$MOUNTPOINT"
