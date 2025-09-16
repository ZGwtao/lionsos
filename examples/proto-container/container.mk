#
# Copyright 2025, UNSW
#
# SPDX-License-Identifier: BSD-2-Clause
#
IMAGES := \
	timer_driver.elf \
	eth_driver.elf \
	monitor.elf \
	frontend.elf \
	fat.elf \
	serial_driver.elf \
	serial_virt_rx.elf \
	serial_virt_tx.elf \
	blk_virt.elf \
	blk_driver.elf

ifeq ($(strip $(MICROKIT_BOARD)), maaxboard)
	BLK_DRIV_DIR := mmc/imx
	SERIAL_DRIV_DIR := imx
	TIMER_DRIV_DIR := imx
	CPU := cortex-a53
else ifeq ($(strip $(MICROKIT_BOARD)), qemu_virt_aarch64)
	BLK_DRIV_DIR := virtio
	SERIAL_DRIV_DIR := arm
	TIMER_DRIV_DIR := arm
	IMAGES += blk_driver.elf
	CPU := cortex-a53
	QEMU := qemu-system-aarch64
else
$(error Unsupported MICROKIT_BOARD given)
endif

# Toolchain and compiler settings
TOOLCHAIN := aarch64-none-elf
CPU := cortex-a53

CC := $(TOOLCHAIN)-gcc
LD := $(TOOLCHAIN)-ld
AS := $(TOOLCHAIN)-as
AR := $(TOOLCHAIN)-ar
RANLIB := $(TOOLCHAIN)-ranlib
OBJCOPY := $(TOOLCHAIN)-objcopy
TARGET := aarch64-none-elf
MICROKIT_TOOL ?= $(MICROKIT_SDK)/bin/microkit
DTC := dtc
PYTHON ?= python3

BOARD_DIR := $(MICROKIT_SDK)/board/$(MICROKIT_BOARD)/$(MICROKIT_CONFIG)
SDDF := $(LIONSOS)/dep/sddf

METAPROGRAM := $(CONTAINER_DIR)/meta.py
DTS := $(SDDF)/dts/$(MICROKIT_BOARD).dts
DTB := $(MICROKIT_BOARD).dtb

FAT := $(LIONSOS)/components/fs/fat
MUSL_SRC := $(LIONSOS)/dep/musllibc
MUSL := musllibc

CFLAGS := \
	-mtune=$(CPU) \
	-mstrict-align \
	-ffreestanding \
	-g3 \
	-O3 \
	-Wall \
	-Wno-unused-function \
	-I$(BOARD_DIR)/include \
	-DBOARD_$(MICROKIT_BOARD) \
	-I$(LIONSOS)/include \
	-I$(SDDF)/include \
	-I$(SDDF)/include/microkit


LDFLAGS := -L$(BOARD_DIR)/lib
LIBS := -lmicrokit -Tmicrokit.ld libsddf_util_debug.a

SYSTEM_FILE := container.system
IMAGE_FILE := container.img
REPORT_FILE := report.txt

all: cache.o
CHECK_FLAGS_BOARD_MD5:=.board_cflags-$(shell echo -- ${CFLAGS} ${BOARD} ${MICROKIT_CONFIG} | shasum | sed 's/ *-//')

${CHECK_FLAGS_BOARD_MD5}:
	-rm -f .board_cflags-*
	touch $@


%.elf: %.o
	${LD} ${LDFLAGS} -o $@ $< ${LIBS}

BLK_DRIVER := $(SDDF)/drivers/blk/${BLK_DRIV_DIR}
BLK_COMPONENTS := $(SDDF)/blk/components

include ${SDDF}/util/util.mk
include ${SDDF}/drivers/timer/${TIMER_DRIV_DIR}/timer_driver.mk
include ${SDDF}/drivers/serial/${SERIAL_DRIV_DIR}/serial_driver.mk
include ${SDDF}/serial/components/serial_components.mk
include ${SDDF}/libco/libco.mk
include ${BLK_DRIVER}/blk_driver.mk
include ${BLK_COMPONENTS}/blk_components.mk

%.py: ${CONTAINER_DIR}/%.py
	cp $< $@

FAT_LIBC_LIB := $(MUSL)/lib/libc.a
FAT_LIBC_INCLUDE := $(MUSL)/include
CONTAINER_LIBC_LIB := $(FAT_LIBC_LIB)
CONTAINER_LIBC_INCLUDE := $(FAT_LIBC_INCLUDE)
include $(LIONSOS)/components/fs/fat/fat.mk
include $(LIONSOS)/components/proto-container/pc.mk

$(MUSL):
	mkdir -p $@

MUSL_CONFIGURED := ${MUSL}/.configured
MUSL_BUILT := ${MUSL}/.built

$(MUSL_CONFIGURED): $(MUSL)
	cd ${MUSL} && \
	CC=aarch64-none-elf-gcc \
	CROSS_COMPILE=aarch64-none-elf- \
	${MUSL_SRC}/configure \
		--srcdir=${MUSL_SRC} \
		--prefix=${abspath ${MUSL}} \
		--with-malloc=oldmalloc \
		--target=aarch64 \
		--enable-warnings \
		--disable-shared \
		--enable-static
	touch $@

$(MUSL_BUILT): $(MUSL_CONFIGURED)
	${MAKE} -C ${MUSL} install
	touch $@

$(CONTAINER_LIBC_LIB) $(CONTAINER_LIBC_INCLUDE): $(MUSL_BUILT)

${IMAGES}: libsddf_util_debug.a

%.o: %.c
	${CC} ${CFLAGS} -c -o $@ $<

FORCE:

%.elf: %.o
	${LD} ${LDFLAGS} -o $@ $< ${LIBS}

$(DTB): $(DTS)
	$(DTC) -q -I dts -O dtb $(DTS) > $(DTB)

$(SYSTEM_FILE): $(METAPROGRAM) $(IMAGES) $(DTB)
	$(PYTHON) $(METAPROGRAM) --sddf $(SDDF) --board $(MICROKIT_BOARD) --dtb $(DTB) --output . --sdf $(SYSTEM_FILE)
	$(OBJCOPY) --update-section .device_resources=serial_driver_device_resources.data serial_driver.elf
	$(OBJCOPY) --update-section .serial_driver_config=serial_driver_config.data serial_driver.elf
	$(OBJCOPY) --update-section .serial_virt_tx_config=serial_virt_tx.data serial_virt_tx.elf
	$(OBJCOPY) --update-section .serial_virt_rx_config=serial_virt_rx.data serial_virt_rx.elf
	$(OBJCOPY) --update-section .device_resources=timer_driver_device_resources.data timer_driver.elf
	$(OBJCOPY) --update-section .timer_client_config=timer_client_frontend.data frontend.elf
	$(OBJCOPY) --update-section .serial_client_config=serial_client_frontend.data frontend.elf
#	$(OBJCOPY) --update-section .fs_client_config=fs_client_micropython.data micropython.elf
	$(OBJCOPY) --update-section .device_resources=blk_driver_device_resources.data blk_driver.elf
	$(OBJCOPY) --update-section .blk_driver_config=blk_driver.data blk_driver.elf
	$(OBJCOPY) --update-section .blk_virt_config=blk_virt.data blk_virt.elf
	$(OBJCOPY) --update-section .blk_client_config=blk_client_fatfs.data fat.elf
	$(OBJCOPY) --update-section .fs_server_config=fs_server_fatfs.data fat.elf

$(IMAGE_FILE) $(REPORT_FILE): $(IMAGES) $(SYSTEM_FILE)
	$(MICROKIT_TOOL) $(SYSTEM_FILE) \
		--search-path $(BUILD_DIR) --board $(MICROKIT_BOARD) 	\
		--config $(MICROKIT_CONFIG) -o $(IMAGE_FILE) -r $(REPORT_FILE)

qemu_disk:
	$(LIONSOS)/dep/sddf/tools/mkvirtdisk $@ 1 512 16777216

qemu: ${IMAGE_FILE} qemu_disk
	$(QEMU) -machine virt,virtualization=on \
		-cpu cortex-a53 \
		-serial mon:stdio \
		-device loader,file=$(IMAGE_FILE),addr=0x70000000,cpu-num=0 \
		-m size=2G \
		-nographic \
		-global virtio-mmio.force-legacy=false \
		-d guest_errors \
		-drive file=qemu_disk,if=none,format=raw,id=hd \
		-device virtio-blk-device,drive=hd
