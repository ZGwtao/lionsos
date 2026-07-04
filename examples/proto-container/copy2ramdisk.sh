#!/usr/bin/env bash
# Usage: ./copy2ramdisk.sh <file> <partition_number>

set -euo pipefail

if [ "$#" -ne 2 ]; then
    echo "Usage: $0 <file> <partition_number>" >&2
    exit 1
fi

FILE=$1
PART=$2

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
DISK="${SCRIPT_DIR}/build/qemu_disk"

if [ ! -f "$FILE" ]; then
    echo "Error: source file does not exist: $FILE" >&2
    exit 1
fi

if [ ! -f "$DISK" ]; then
    echo "Error: disk image does not exist: $DISK" >&2
    exit 1
fi

if ! [[ "$PART" =~ ^[1-9][0-9]*$ ]]; then
    echo "Error: invalid partition number: $PART" >&2
    exit 1
fi

for command in sfdisk jq mcopy; do
    if ! command -v "$command" >/dev/null 2>&1; then
        echo "Error: required command not found: $command" >&2
        exit 1
    fi
done

DISK_JSON=$(sfdisk --json "$DISK")

PARTITION_JSON=$(
    jq -c --argjson index "$((PART - 1))" \
        '.partitiontable.partitions[$index]' \
        <<<"$DISK_JSON"
)

if [ -z "$PARTITION_JSON" ] || [ "$PARTITION_JSON" = "null" ]; then
    echo "Error: partition $PART does not exist in $DISK" >&2
    exit 1
fi

START_SECTOR=$(jq -r '.start' <<<"$PARTITION_JSON")
SECTOR_SIZE=$(jq -r '.partitiontable.sectorsize // 512' <<<"$DISK_JSON")

if ! [[ "$START_SECTOR" =~ ^[0-9]+$ ]]; then
    echo "Error: failed to determine partition start sector" >&2
    exit 1
fi

if ! [[ "$SECTOR_SIZE" =~ ^[0-9]+$ ]]; then
    echo "Error: failed to determine disk sector size" >&2
    exit 1
fi

OFFSET=$((START_SECTOR * SECTOR_SIZE))

mcopy \
    -o \
    -i "${DISK}@@${OFFSET}" \
    "$FILE" \
    "::/$(basename "$FILE")"

echo "Copied $(basename "$FILE") to partition $PART"