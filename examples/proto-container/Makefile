#
# Copyright 2025, UNSW
#
# SPDX-License-Identifier: BSD-2-Clause
#

ifeq ($(strip $(MICROKIT_SDK)),)
$(error MICROKIT_SDK must be specified)
endif
override MICROKIT_SDK:=$(abspath ${MICROKIT_SDK})

export LIONSOS ?= $(abspath ../..)
export CONTAINER_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
export CONFIG_INCLUDE ?= $(abspath src/config)
export MICROKIT_CONFIG ?= debug
export BUILD_DIR ?= $(abspath build)
export MICROKIT_BOARD ?= qemu_virt_aarch64

IMAGE_FILE := $(BUILD_DIR)/container.img
REPORT_FILE := $(BUILD_DIR)/report.txt

all: ${IMAGE_FILE}

qemu ${IMAGE_FILE} ${REPORT_FILE} system clean qemu_disk clobber: ${BUILD_DIR}/Makefile FORCE
	${MAKE} -C ${BUILD_DIR} MICROKIT_SDK=${MICROKIT_SDK} $(notdir $@)

${BUILD_DIR}/Makefile: container.mk
	mkdir -p ${BUILD_DIR}
	cp container.mk $@

${BUILD_DIR}/Makefile: container.mk Makefile
	mkdir -p ${BUILD_DIR}
	echo "export LIONSOS ?= ${LIONSOS}" > $@
	echo "export CONTAINER_DIR ?= ${CONTAINER_DIR}" >> $@
	echo "export BUILD_DIR := ${BUILD_DIR}" >> $@
	echo "export MICROKIT_BOARD ?= ${MICROKIT_BOARD}" >> $@
	echo "export MICROKIT_SDK ?= ${MICROKIT_SDK}" >> $@
	echo "export MICROKIT_CONFIG ?= ${MICROKIT_CONFIG}" >> $@
	cat container.mk >> $@

submodules:
	git submodule update --init $(LIONSOS)/dep/musllibc
	git submodule update --init $(LIONSOS)/dep/sddf
	git submodule update --init $(LIONSOS)/dep/libmicrokitco
	git submodule update --init $(LIONSOS)/dep/libtrustedlo

FORCE:
