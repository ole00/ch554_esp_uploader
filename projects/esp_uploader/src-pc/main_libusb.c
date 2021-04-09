/* Flash multiple partitions example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include "esp_loader.h"
#include "example_common.h"
#include "libusb_port.h"

#include "serial_io.h"

#define DEFAULT_BAUD_RATE 74880
#define HIGHER_BAUD_RATE  74880

#define APPLICATION_ADDRESS 0x10000
#define BOOTLOADER_ADDRESS 0x1000
#define PARTITION_ADDRESS 0x8000

static void upload_file(const char *path, size_t address)
{
    char *buffer = NULL;

    FILE *image = fopen(path, "r");
    if (image == NULL) {
        printf("Error:Failed to open file %s\n", path);
        return;
    }

    fseek(image, 0L, SEEK_END);
    size_t size = ftell(image);
    rewind(image);

    printf("File %s opened. Size: %u bytes\n", path, (uint32_t)size);

    buffer = (char *)malloc(size);
    if (buffer == NULL) {
        printf("Error: Failed allocate memory\n");
        goto cleanup;
    }

    // copy file content to buffer
    size_t bytes_read = fread(buffer, 1, size, image);
    if (bytes_read != size) {
        printf("Error occurred while reading file");
        goto cleanup;
    }

    flash_binary(buffer, size, address);

cleanup:
    fclose(image);
    free(buffer);
}


int main(int argc, char** argv)
{
	int i;
    loader_usb_config_t config;
    char* fw_path = NULL;
    char* bl_path = NULL;
    char* pt_path = NULL;
    char* ar_path = NULL;

    if (argc < 3) {
        printf("usage: %s [-a app.ino.bin] [-b bootloader.bin] [-p partitions.bin] [-f firmware.bin] \n", argv[0]);
        return 1;
    }
    
    for (i = 1; i < argc; i++) {
    	char* arg = argv[i];
    	if (!strcmp("-a", arg)) {
    		ar_path = argv[++i];
    	} else
    	if (!strcmp("-b", arg)) {
    		bl_path = argv[++i]; 
    	} else
    	if (!strcmp("-p", arg)) {
    		pt_path = argv[++i]; 
    	} else
    	if (!strcmp("-f", arg)) {
    		fw_path = argv[++i]; 
    	}
    }
    if (ar_path == NULL && fw_path == NULL && bl_path == NULL && pt_path == NULL) {
    	printf("No file specified\n");
    	return 1;
    }

	config.c = NULL;
	config.h = NULL;
    config.baudrate = DEFAULT_BAUD_RATE;

    loader_port_usb_init(&config);

    if (connect_to_target(HIGHER_BAUD_RATE) == ESP_LOADER_SUCCESS)
	{
		if (ar_path != NULL) {
			upload_file(ar_path, 0);
		}
		if (bl_path != NULL) {
			upload_file(bl_path, BOOTLOADER_ADDRESS);
		}
		if (pt_path != NULL) {
            upload_file(pt_path, PARTITION_ADDRESS); 
		}
		if (fw_path != NULL) {
            upload_file(fw_path, APPLICATION_ADDRESS);
        } 
    }

    loader_port_reset_target();
    return 0;
}
