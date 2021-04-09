TARGET = esp_uploader
FREQ_SYS=16000000

EXTRA_FLAGS =  -DDEV_BOARD --std-sdcc99

C_FILES = \
	../src/main.c \
	../../../include/debug.c


pre-flash:
	

MK_ROOT_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

include $(MK_ROOT_DIR)/../Makefile.include
