#!/bin/bash

# pip install --force-reinstall "sdfgen==0.24.0"

make \
  MICROKIT_SDK=~/wsp/microkit/release/microkit-sdk-2.0.1-dev \
  MICROKIT_BOARD=qemu_virt_aarch64 \
  qemu
