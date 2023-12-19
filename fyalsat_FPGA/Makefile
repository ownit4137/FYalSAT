SHELL := /bin/bash  #
MK_PATH := $(abspath $(lastword $(MAKEFILE_LIST)))
COMMON_REPO ?= $(shell bash -c 'export MK_PATH=$(MK_PATH); echo $${MK_PATH%fyalsat_FPGA/*}')
PWD = $(shell readlink -f .)
XF_PROJ_ROOT = $(shell readlink -f $(COMMON_REPO))

PLATFORM_BLOCKLIST += nodma 
PLATFORM ?= xilinx_u250_gen3x16_xdma_4_1_202210_1 # xilinx_u280_xdma_201920_3
DEV_ARCH := $(shell platforminfo -p $(PLATFORM) | grep 'FPGA Family' | sed 's/.*://' | sed '/ai_engine/d' | sed 's/^[[:space:]]*//')
CPU_TYPE := $(shell platforminfo -p $(PLATFORM) | grep 'CPU Type' | sed 's/.*://' | sed '/ai_engine/d' | sed 's/^[[:space:]]*//')

ifeq ($(CPU_TYPE), cortex-a9)
HOST_ARCH := aarch32
else ifneq (,$(findstring cortex-a, $(CPU_TYPE)))
HOST_ARCH := aarch64
else
HOST_ARCH := x86
endif

include makefile_us_alveo.mk