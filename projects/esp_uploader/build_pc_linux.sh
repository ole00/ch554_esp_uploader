
CFLAGS="-g -Isrc-pc  -DMD5_ENABLED=1  -DSINGLE_TARGET_SUPPORT"

gcc -o pc_upl ${CFLAGS} src-pc/esp_loader.c src-pc/esp_targets.c src-pc/md5_hash.c src-pc/serial_comm.c \
		src-pc/libusb_port.c src-pc/example_common.c src-pc/main_libusb.c \
		-lusb-1.0
