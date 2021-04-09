/* Copyright 2020 Espressif Systems (Shanghai) PTE LTD
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "serial_io.h"
#include "serial_comm.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <termios.h>
#include <time.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/param.h>


#ifdef MINGW
#include <libusbx-1.0/libusb.h>
#else
#include <libusb-1.0/libusb.h>
#endif

#include "libusb_port.h"

static const char *const strings[2] = { "info", "fatal" };
static void infoAndFatal(const int s, char *f, ...) {
    va_list ap;
    va_start(ap,f);
    fprintf(stderr, "app: %s: ", strings[s]);
    vfprintf(stderr, f, ap);
    va_end(ap);
    if (s) exit(s);
}

#define info(...)   infoAndFatal(0, __VA_ARGS__)
#define fatal(...)  infoAndFatal(1, __VA_ARGS__)


// #define SERIAL_DEBUG_ENABLE

#ifdef SERIAL_DEBUG_ENABLE

static void serial_debug_print(const uint8_t *data, uint16_t size, bool write)
{
    static bool write_prev = false;
    uint8_t hex_str[3];

    if (write_prev != write) {
        write_prev = write;
        printf("\n--- %s ---\n", write ? "WRITE" : "READ");
    }

    for (uint32_t i = 0; i < size; i++) {
        printf("%02x ", data[i]);
    }
}

#else

static void serial_debug_print(const uint8_t *data, uint16_t size, bool write) { }

#endif



#define VENDOR_ID 0x16c0 
#define PRODUCT_ID 0x05dc
#define VENDOR_NAME "github.com/ole00"
#define PRODUCT_NAME "esp_upl"

//see usb1.1 page 183: value bitmap: Host->Device, Vendor request, Recipient is interface
#define TYPE_OUT_ITF		0x41

//see usb1.1 page 183: value bitmap: Device->Host, Vendor request, Sender is interface
#define TYPE_IN_ITF		(0x41 | (1 << 7))

#define MAX_PACKET_LEN 32
#define COMMAND_GET_PROGRESS 0
#define COMMAND_READ_UART  0x01
#define COMMAND_WRITE_UART 0x02
#define COMMAND_SET_GPIO   0x03
#define COMMAND_SET_BAUDR  0x04

loader_usb_config_t *cfg;
static int64_t s_time_end;
static char verbose = 0; 

static uint8_t outBuf[MAX_PACKET_LEN]; //output (command) buffer
static uint8_t resBuf[MAX_PACKET_LEN]; //input (response) buffer
int resBufPos = 0;
int resBufMax = 0;

uint8_t writeBuf[4* 1024];
int writeBufPos;
int readDelay;

int writeStatCnt;
int writeStatTotal;
int writeStatMin;
int writeStatMax;
int writeStat[33];

static int dumpBuffer(uint8_t* buf, int size) {
    int i;
    for (i = 0; i < size; i++) {
        printf("%02X ", buf[i]);
        if (i % 16 == 15) {
            printf("\n");
        }
    }
    printf("\n");
    return 0;
}

static int sendControlTransfer(libusb_device_handle *h, uint8_t command, uint16_t param1, uint16_t param2, uint8_t len) {
    int ret;

    ret = libusb_control_transfer(h, TYPE_OUT_ITF, command, param1, param2, outBuf, len, 80);
    if (verbose) {
        info("control transfer out:  result=%i \n", ret);
    }
    return ret;
}

static int recvControlTransfer(libusb_device_handle *h, uint8_t command, uint16_t param1, uint16_t param2) {
    int ret;
    memset(resBuf, 0, sizeof(resBuf));

    ret = libusb_control_transfer(h, TYPE_IN_ITF, command, param1, param2, resBuf, sizeof(resBuf), 80);
    if (verbose) {
        info("control transfer (0x%02x) incoming:  result=%i\n", command, ret);
        dumpBuffer(resBuf, sizeof(resBuf));
    }
    return ret;
}

//try to find the programmer usb device
static libusb_device_handle* getDeviceHandle(libusb_context* c) {
    int max;
    int ret;
    int device_index = -1;
    int i;
    libusb_device** dev_list = NULL;
    struct libusb_device_descriptor des;
    struct libusb_device_handle* handle;

    ret = libusb_get_device_list(c, &dev_list);
    if (verbose) {
        info("total USB devices found: %i \n", ret);
    }
    max = ret;
    //print all devices
    for (i = 0; i < max; i++) {
        char vendorName[32];
        char productName[32];
        ret = libusb_get_device_descriptor(dev_list[i],  & des);
        if (des.idVendor == VENDOR_ID && des.idProduct == PRODUCT_ID) {

            //get the device handle in order to get the vendor name and product name
            ret = libusb_open(dev_list[i], &handle);
            if (verbose) {
                info("open device result=%i\n", ret);
            }
            if (ret) {
                libusb_free_device_list(dev_list, 1);
                fatal("device open failed\n");
            }

            //retrieve the texts
            vendorName[0] = 0;
            libusb_get_string_descriptor_ascii(handle, des.iManufacturer, vendorName, sizeof(vendorName));
            vendorName[sizeof(vendorName) - 1] = 0;
            productName[0] = 0;
            libusb_get_string_descriptor_ascii(handle, des.iProduct, productName, sizeof(productName));
            productName[sizeof(productName) - 1] = 0;

            libusb_close(handle);

            if (verbose) {
                info("device %i  vendor=%04x, product=%04x bus:device=%i:%i %s/%s\n",
                        i, des.idVendor, des.idProduct,
                        libusb_get_bus_number(dev_list[i]),
                        libusb_get_device_address(dev_list[i]),
                        vendorName, productName
                );
            }
            
            //ensure the vendor name and product name matches
            if (
                device_index == -1 &&
                strcmp(VENDOR_NAME, vendorName) == 0 &&
                strcmp(PRODUCT_NAME, productName) == 0
            ) {
                device_index = i;
            }
        }
    }

    if (device_index < 0) {
        libusb_free_device_list(dev_list, 1);
        fatal("no device found\n");
    }

    if (verbose) {
        info("using device: %i \n", device_index);
    }

    ret = libusb_open(dev_list[device_index], &handle);
    if (verbose) {
        info("open device result=%i\n", ret);
    }
    if (ret) {
        libusb_free_device_list(dev_list, 1);
        fatal("device open failed\n");
    }

    libusb_free_device_list(dev_list, 1);

#if 0
    //get config
    ret = libusb_get_descriptor(handle, LIBUSB_DT_DEVICE, 0, descriptor, 18);
    if (verbose) {
        info("get device descriptor 0 result=%i\n", ret);
    }
    ret = libusb_get_descriptor(handle, LIBUSB_DT_CONFIG, 0, descriptor, 255);
    if (verbose) {
        info("get device configuration 0 result=%i\n", ret);
    }
    usleep(20*1000);
#endif
    return handle;
}



static int usbOpen (loader_usb_config_t *config)
{

	cfg = config;
    //initialize libusb 
    if (libusb_init(&cfg->c)) {
        fatal("can not initialise libusb\n");
    }
    //set debugging state
#ifdef SERIAL_DEBUG_ENABLE
    libusb_set_debug(cfg->c, 4);
#endif
    
       //get the handle of the connected USB device
    cfg->h = getDeviceHandle(cfg->c);

    //try to detach existing kernel driver if kernel is already handling 
    //the device
    if (libusb_kernel_driver_active(cfg->h, 0) == 1) {
        if (verbose) {
            info("kernel driver active\n");
        }
        if (!libusb_detach_kernel_driver(cfg->h, 0)) {
            if (verbose) {
                info("driver detached\n");
            }
        }
    }


    //set the first configuration -> initialize USB device
    if (libusb_set_configuration (cfg->h, 1) != 0) {
        fatal("cannot set device configuration\n");
    }

    if (verbose) {
        info("device configuration set\n");
    }
    usleep(20 * 1000);

    //get the first interface of the USB configuration
    if (libusb_claim_interface(cfg->h, 0) < 0) {
         fatal("cannot claim interface\n");
    }

    if (verbose) {
        info("interface claimed\n");
    }

    if (libusb_set_interface_alt_setting(cfg->h, 0, 0) < 0) {
        fatal("alt setting failed\n");
    }

    
    usleep (10000) ;  // 10mS

    return 0 ;
}


esp_loader_error_t loader_port_usb_init(loader_usb_config_t *config)
{
	int i;
    int ret = usbOpen(config);
    if (ret < 0) {
        printf("Usb device could not be opened!\n");
        return ESP_LOADER_ERROR_FAIL;
    }

	//loader_port_change_baudrate(74880);
	writeBufPos = 0;
	readDelay = 0;

	writeStatCnt = 0;
	writeStatTotal = 0;
	writeStatMin = 0xFFFFFF;
	writeStatMax = 0;
	for (i = 1; i < 33; i++) {
		writeStat[i] = 0;
	}



    return ESP_LOADER_SUCCESS;
}

static int usbIoFinished(libusb_device_handle* h)
{
    int ret = recvControlTransfer(h, COMMAND_GET_PROGRESS, 0, 0);
    if (ret != 1) {
    	if (verbose) {
        	info("Get progress/status failed. result=%i\n", ret);
        } 
    }
    //printf("buf 1 = 0x%02x\n", resBuf[1]);

    return resBuf[0]; 
}
static int waitForFinish(libusb_device_handle* h, int initialDelay, int step, int errorState, int timeout)
{
	int i = 0;
    usleep(initialDelay);
    timeout -= initialDelay;
    if (timeout < 0) {
    	timeout = 1;
    }
    while (timeout > 0) {
        int ret = usbIoFinished(h);
        if (ret == 0) {
        	//printf(" wait to finish=%i\n", i);
            return timeout;
        }
        if (errorState != 0 && ret == errorState) {
            return ret;
        }
        timeout -= step;
        if (timeout > 0) {
        	usleep(step);
        	i++;
        }
    }
    return 0; //time expired
}

static int writeUart(const uint8_t* data, int size, int timeout)
{
    int result = size;
    int waitTime = 1000;

    uint32_t pos = 0;
    uint16_t blk = 0;
    
    timeout *= 1000;
    
    libusb_device_handle* h = cfg->h;
    
    //accumulate data to a write buffer
    memcpy(writeBuf + writeBufPos, data, size);
    writeBufPos += size;
    
	return size;

}

static int flushUart(int timeout)
{
    int result;
    int size;

    uint32_t pos = 0;
    uint16_t blk = 0;
    
    timeout *= 1000;
    
    libusb_device_handle* h = cfg->h;
    
  
    size = writeBufPos;
    writeBufPos = 0;
	result = size;
	
	readDelay = 1; //after flushing comes a read

	//printf("* Write flush: size=%i \n", size);
    while (size > 0 && timeout > 0) {
    	    	
        //dumpBuffer(outBuf, size);
        blk = size > MAX_PACKET_LEN ? MAX_PACKET_LEN: size;
        memcpy(outBuf, writeBuf + pos, blk);
        
        int ret = sendControlTransfer(h, COMMAND_WRITE_UART, 0, 0 , blk);
        if (verbose) {
        	info("Write chunk result=%i (%s) %i \n", ret, ret == blk ? "OK" : "Failed", pos);
		}
		if (ret < 0) {
			result = -1;
			break;
		} else
		if (ret > 0) {
		    pos += blk;
		    size -= blk;
		    
		    writeStatCnt++;
		    writeStatTotal += ret;
		    if (ret < writeStatMin) {
		    	writeStatMin = ret; 
		    }
		    if (ret > writeStatMax) {
		    	writeStatMax = ret;
		    }
		    writeStat[ret]++;
		    if (ret != blk) {
		    	info("incorrect bytes written\n");
		    }
		}
		timeout -= 100;

		
		//check previous write operation has finished
    	ret = waitForFinish(h, 2000, 1000, 0, timeout);
        if (ret < 0) {
            info("\nError writing to flash at pos=%i\n", pos);
            result = -1;
            break;
        }
        //time out
        if (ret == 0) {
        	printf("\nwrite: time out 0\n");
        	return 0;
        }
    
    	timeout = ret;

    }
    if (timeout <= 0) {
    	printf("\nwrite: time out 1\n");
    }
    return result;    
}

//duration in milli-seconds
static int readUart(uint8_t *data, int size, int duration) {
	int total = 0;
	int statNoReads = 0; //Number of reads
	int statNoBytes = 0; //total number of bytes read
	int statNoEmpty = 0; //Number of empty reads
	libusb_device_handle* h = cfg->h;
	int dataPos = 0;
	
	duration *=  1000;

	if (readDelay) {
		//printf("read delay!\n");
		readDelay = 0;
		usleep(7000); //give time to read the whole buffer before interrupting with USB
	}
	//printf("* Read: size=%i \n", size);
	
	//copy data from the reception buffer
	while (resBufPos < resBufMax && dataPos < size) {
		data[dataPos] = resBuf[resBufPos];
		dataPos++;
		resBufPos++;
	}
	if (dataPos == size) {
		return 0;
	}
	

	//printf("   read USB..");

	while (total < duration && statNoBytes < (size - dataPos)) {
		int ret = recvControlTransfer(h, COMMAND_READ_UART, 0, 0);
    	if (ret < 0) {
        	info("read uart failed. result=%i\n", ret);
        	return 0; //timeout
        }
        if (ret > 0) {
        	int i;
        	resBufPos = 0;
        	resBufMax = ret;
        	
        	statNoReads++;
        	statNoBytes += ret;
        	
        	//printf("read size=%i\n", ret);

			//copy data from the reception buffer
			while (resBufPos < resBufMax && dataPos < size) {
				data[dataPos] = resBuf[resBufPos];
				dataPos++;
				resBufPos++;
			}
			if (dataPos == size) {
				return 0;
			}
        	
        	usleep(400);
        	total += 400;
        } else {
        	//printf("read: no data...\n");
        	statNoEmpty++;
        	total +=500; // the deault usb recv timeout
        	usleep(1500);
        }
    }
    
#if 0
    if (statNoReads == 0) {
    	statNoReads = 1;
    }
    total = statNoBytes * 100;
    printf("Stat: no. reads=%i no. bytes=%i bytes-per-read=%i.%i empty reads=%i\n", statNoReads, statNoBytes,
    	statNoBytes / statNoReads, (total / statNoReads) % 100, statNoEmpty);
#endif

	printf("\nread: time out 0\n");
	return 2; //timeout
	   
}


esp_loader_error_t loader_port_serial_write(const uint8_t *data, uint16_t size, uint32_t timeout)
{
    serial_debug_print(data, size, true);

    int written = writeUart(data, size, timeout);

    if (written < 0) {
        return ESP_LOADER_ERROR_FAIL;
    } else if (written < size) {
        return ESP_LOADER_ERROR_TIMEOUT;
    } else {
        return ESP_LOADER_SUCCESS;
    }
}

esp_loader_error_t loader_port_write_flush(void)
{
    //serial_debug_print(data, size, true);

    int written = flushUart(5000);

    if (written < 0) {
        return ESP_LOADER_ERROR_FAIL;
    } else if (written == 0) {
        return ESP_LOADER_ERROR_TIMEOUT;
    } else {
        return ESP_LOADER_SUCCESS;
    }
}

esp_loader_error_t loader_port_serial_read(uint8_t *data, uint16_t size, uint32_t timeout)
{
    RETURN_ON_ERROR( readUart(data, size, timeout) );

    serial_debug_print(data, size, false);

    return ESP_LOADER_SUCCESS;
}


// Set GPIO0 LOW, then assert reset pin for 50 milliseconds.
void loader_port_enter_bootloader(void)
{   
    libusb_device_handle* h = cfg->h;

    int ret;

    printf("enter bootloader\n");
    //      bits: 0         1         2
    // 1 => Boot: 0, Reset: 0, Enable:0 
    ret = sendControlTransfer(h, COMMAND_SET_GPIO, 0, 0, 0);
    if (ret != 0) {
        info("GPIO set failed. result=%i\n", ret); 
    }
    loader_port_delay_ms(150);

    //      bits: 0        1         2
    // 5 => Boot:0, Reset: 1, Enable:1
    ret = sendControlTransfer(h, COMMAND_SET_GPIO, 6, 0, 0);
    if (ret != 0) {
        info("GPIO set failed. result=%i\n", ret); 
    }

    loader_port_delay_ms(4);
}

static void printStats(void) {
	int i;
	if (writeStatCnt == 0) {
		writeStatCnt = 1;
	}
	printf("Write stats: cnt=%i total=%i avg=%i min=%i max=%i\n",
		writeStatCnt, writeStatTotal, writeStatTotal / writeStatCnt, writeStatMin, writeStatMax
	);
	for (i = 1; i < 33; i++) {
		printf(" * %i : %i\n", i, writeStat[i]);
	}

}

void loader_port_reset_target(void)
{
    libusb_device_handle* h = cfg->h;
    int ret;
    
    printf("reset target\n");
    
    //      bits: 0        1         2
    // 0 => Boot:0, Reset: 0, Enable:0 
    ret = sendControlTransfer(h, COMMAND_SET_GPIO, 0, 0, 0);
    if (ret != 0) {
        info("GPIO set failed. result=%i\n", ret); 
    }
    loader_port_delay_ms(100);

    //      bits: 0        1         2
    // 7 => Boot:1, Reset: 1, Enable:1 
    ret = sendControlTransfer(h, COMMAND_SET_GPIO, 7, 0, 0);
    if (ret != 0) {
        info("GPIO set failed. result=%i\n", ret); 
    }
    
    //printStats();
}


void loader_port_delay_ms(uint32_t ms)
{
    usleep(ms * 1000);
}


void loader_port_start_timer(uint32_t ms)
{
    s_time_end = clock() + (ms * (CLOCKS_PER_SEC / 1000));
}


uint32_t loader_port_remaining_time(void)
{
    int64_t remaining = (s_time_end - clock()) / 1000;
    return (remaining > 0) ? (uint32_t)remaining : 0;
}


void loader_port_debug_print(const char *str)
{
    printf("DEBUG: %s\n", str);
}

esp_loader_error_t loader_port_change_baudrate(uint32_t baudrate)
{
	int ret;
	int data;
	libusb_device_handle* h = cfg->h;
	
	
	if (baudrate == 74880) {
		data = 0;
	} else {
		data = 1; // baud rate : 115200;
	}
	printf("setting baud rate: %i\n", baudrate);
	ret = sendControlTransfer(h, COMMAND_SET_BAUDR, data, 0, 0);
	if (ret != 0) {
        info("baud rate set failed. result=%i\n", ret); 
    } else {
		loader_port_delay_ms(40);
	}
    return 0; 
}
