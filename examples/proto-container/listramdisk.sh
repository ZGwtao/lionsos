#!/usr/bin/env bash
# Usage: ./listramdisk.sh [disk_image]

set -euo pipefail

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
DISK=${1:-"${SCRIPT_DIR}/build/qemu_disk"}

if [ ! -f "$DISK" ]; then
    echo "Error: disk image does not exist: $DISK" >&2
    exit 1
fi

for command in sfdisk jq mdir; do
    if ! command -v "$command" >/dev/null 2>&1; then
        echo "Error: required command not found: $command" >&2
        exit 1
    fi
done

DISK_JSON=$(sfdisk --json "$DISK")

SECTOR_SIZE=$(
    jq -r '.partitiontable.sectorsize // 512' <<<"$DISK_JSON"
)

PARTITION_COUNT=$(
    jq '.partitiontable.partitions | length' <<<"$DISK_JSON"
)

echo "Disk image: $DISK"
echo

echo "Partition table:"
sfdisk --list "$DISK"

echo
echo "Filesystem contents:"

for ((INDEX = 0; INDEX < PARTITION_COUNT; INDEX++)); do
    PART=$((INDEX + 1))

    START_SECTOR=$(
        jq -r \
            --argjson index "$INDEX" \
            '.partitiontable.partitions[$index].start' \
            <<<"$DISK_JSON"
    )

    SECTOR_COUNT=$(
        jq -r \
            --argjson index "$INDEX" \
            '.partitiontable.partitions[$index].size' \
            <<<"$DISK_JSON"
    )

    OFFSET=$((START_SECTOR * SECTOR_SIZE))
    SIZE_BYTES=$((SECTOR_COUNT * SECTOR_SIZE))

    echo
    echo "============================================================"
    echo "Partition $PART"
    echo "Start sector : $START_SECTOR"
    echo "Sector count : $SECTOR_COUNT"
    echo "Byte offset  : $OFFSET"
    echo "Size         : $SIZE_BYTES bytes"
    echo "============================================================"

    if ! mdir -i "${DISK}@@${OFFSET}" ::/; then
        echo "Unable to read partition $PART as a FAT filesystem." >&2
    fi
done