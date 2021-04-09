// ESP Firmware uploader via CH552/CH551/CH554
// !!!!  Must use 3.3V VCC !!!!
// CH55X max speed 16MHz when running on 3.3V

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define DEFAULT_ENDP0_SIZE 32

#include <ch554.h>
#include <ch554_usb.h>
#include <bootloader.h>

#define UART0_BAUD 115200
//#define UART0_BAUD 74880
#include <debug.h>

// standard USB descriptor definitions
#include "usb_desc.h"

// custom USB definitions, must be set before the "usb_intr.h" is included
#define USB_CUST_VENDOR_ID                  0x16c0
#define USB_CUST_PRODUCT_ID                 0x05dc
#define USB_CUST_CONF_POWER                 240
#define USB_CUST_VENDOR_NAME_LEN            17
#define USB_CUST_VENDOR_NAME                { 'g','i', 't', 'h', 'u', 'b', '.','c', 'o', 'm', '/', 'o', 'l','e','0','0', 0}
#define USB_CUST_PRODUCT_NAME_LEN           8
#define USB_CUST_PRODUCT_NAME               { 'e', 's', 'p', '_', 'u', 'p', 'l', 0 }

#define USB_CUST_CONTROL_TRANSFER_HANDLER   handleVendorControlTransfer()
#define USB_CUST_CONTROL_DATA_HANDLER       handleVendorDataTransfer()
#define USB_CUST_SUSPEND_HANDLER            /* no code here */

// function declaration for custom USB transfer handlers
static uint16_t handleVendorControlTransfer();
static void handleVendorDataTransfer();

// USB interrupt handlers - does the most of the USB grunt work
#include "usb_intr.h"

// GPIO setup
#define PORT1 0x90
#define PORT3 0xb0

//LED - P1.4
#define LED_PIN 4
SBIT(LED, PORT1, LED_PIN);

//ESP_ENABLE - P3.2
#define PIN_ESP_ENABLE 2
SBIT(ESP_ENABLE, PORT3, PIN_ESP_ENABLE);

//ESP_BOOT - P3.3
#define PIN_ESP_BOOT 3
SBIT(ESP_BOOT, PORT3, PIN_ESP_BOOT);

//ESP_RESET# - P3.4
#define PIN_ESP_RESET 4
SBIT(ESP_RESET, PORT3, PIN_ESP_RESET);



#define COMMAND_GET_PROGRESS 0
#define COMMAND_READ_UART  0x01
#define COMMAND_WRITE_UART 0x02
#define COMMAND_SET_GPIO   0x03
#define COMMAND_SET_BAUDR  0x04

#define COMMAND_JUMP_TO_BOOTLOADER 0xB0

#define PUT_CHAR(C) while (!TI); TI=0; SBUF = C; 

__xdata __at (0x0020) uint8_t uartBufW[DEFAULT_ENDP0_SIZE]; 
__xdata __at (0x0040) uint8_t uartBufR[DEFAULT_ENDP0_SIZE]; 

//volatile __idata uint16_t blinkTime = 250;
volatile __idata uint8_t command;
volatile __idata uint8_t bufLenW;
volatile __idata uint8_t bufLenR;

volatile __idata uint8_t inProgress;
uint8_t data;
uint8_t p1State, p1Pu, p3State, p3Pu;


/*******************************************************************************
* Jump to bootloader
*******************************************************************************/
static void jumpToBootloader()
{
    LED = 1;
    EA = 0;

    IP_EX &= ~bIP_USB; //remove USB interrupt priority
    GPIO_IE &= ~bIE_RXD0_LO; //disable UART0 RX interrupt
    IE_GPIO = 0; //disable GPIO interrupts

    USB_INT_EN = 0;
    USB_CTRL = 0x6;
    mDelaymS(100);

    bootloader();
}

/*******************************************************************************
* Handler of the vendor Control transfer requests sent from the Host to 
* Endpoint 0
*
* Returns : the length of the response that is stored in Ep0Buffer  
*******************************************************************************/

static uint16_t handleVendorControlTransfer()
{
    switch (UsbIntrSetupReq) {
        case COMMAND_GET_PROGRESS : {
            Ep0Buffer[0] = inProgress;
            return 1;
        } break;
        case COMMAND_READ_UART : {
            uint8_t l;
            if (0 == bufLenR) {
                return 0;
            }
            for (l = 0; l< bufLenR;l++) {
                Ep0Buffer[l] = uartBufR[l];
            }
            bufLenR = 0;
            return  l; //'l' is set in the 'for' loop 
        } break;
        case COMMAND_WRITE_UART : {
            //nothing to do, just wait for the data and confirm this transfer by returning 0
        } break;
        case COMMAND_SET_GPIO : {
            data = UsbSetupBuf->wValueL;
            if (data == 0) {
                ESP_RESET = 0; //priority ESP reset
            }
            command = COMMAND_SET_GPIO;
        } break;
        case COMMAND_SET_BAUDR : {
            data = UsbSetupBuf->wValueL;
            command = COMMAND_SET_BAUDR;
        } break;
        //jump to bootloader - remotely triggered from the Host!
        case COMMAND_JUMP_TO_BOOTLOADER : {
           jumpToBootloader();
        } break;
        default:
            return 0xFF; // Command not supported
    } // end of the switch
    return 0; // no data to transfer back to the host
}

static void handleVendorDataTransfer()
{
    switch (UsbIntrSetupReq) {
        // Ah! The data for uart write arrived.
        case COMMAND_WRITE_UART : {
            LED = 1;
            inProgress = 1;
            // copy the contents of the EP0 buffer into the uart write buffer
            memcpy(uartBufW, Ep0Buffer, USB_RX_LEN);
            bufLenW = USB_RX_LEN;
            command = COMMAND_WRITE_UART;
            LED = 0;
        } break;
    }
}

static void setupGPIO()
{
    // Configure pin 1.4 as GPIO output
    P1_MOD_OC &=  ~(1 << LED_PIN);
    P1_DIR_PU |= (1 << LED_PIN);

    //ESP_ENABLE, ESP_RESET, ESP_BOOT: output
    P3_MOD_OC &=  ~((1 << PIN_ESP_ENABLE) | (1 << PIN_ESP_RESET) | (1 << PIN_ESP_BOOT) );
    P3_DIR_PU |= (1 << PIN_ESP_ENABLE) | (1 << PIN_ESP_RESET) | (1 << PIN_ESP_BOOT);
}


// this delay can be interrupted, so we can exit earlier if needed
static uint8_t delayNonBlocking(uint16_t d)
{
    while (d) {
        mDelaymS(1);
        // delay interrupted when a new value is set into the command 
        if (command) {
            return 0;
        }
        d --;
    }
    return 1;
}

void writeUart() {
    uint8_t i = 0;
    uint8_t c;
    while (i < bufLenW) {
        c = uartBufW[i];
        PUT_CHAR(c);
        i++;
    }
    bufLenW = 0;
}

#if 0
void serialOutStr(char* str) {
    while (*str) {
        PUT_CHAR(*str);
        str++;
    }
}
#endif

// interrupt is triggered when the first bit of the serial data
// is transferrred. That is: RX goes Hi->Lo
void UART0_ISR(void) __interrupt (INT_NO_GPIO) {
    uint16_t safety = 0xFFFF;

    //wait until the whole character is read or until
    //the safety counter expires
    while (safety--) {
        if (RI) goto char_ready;
        if (RI) goto char_ready;
        if (RI) goto char_ready;
    }
    return;

char_ready:
    //store the character to the read buffer
    uartBufR[bufLenR] = SBUF;
    //Clear the interrup flag
    RI = 0;
    //position to the next index in the read buffer 
    bufLenR++;

    //handle buffer overfow
    if (bufLenR > 31) {
        bufLenR = 0;
    };
} 

static void setGpio(void) {
    //ESP power-on sequence: VDD, RESET, ENable (See datasheet 5.1 Electrical characteristics)

    //GPIO0
    ESP_BOOT = data & 1;
    data >>= 1;
    mDelaymS(2);

    ESP_RESET =  data & 1;
    data >>= 1;
    mDelaymS(2);

    ESP_ENABLE = data & 1;
}

void main() {

    CfgFsys();   // CH55x main frequency setup
    mDelaymS(5); // wait for the internal crystal to stabilize.

    // configure GPIO ports
    setupGPIO();

    // configure USB
    USBDeviceCfg();


    // enable UART
    mInitSTDIO();

    command = 0;

    //print initial message
    uartBufW[0] = 'H';
    uartBufW[1] = 'i';
    uartBufW[2] = '!';
    uartBufW[3] = '\r';
    uartBufW[4] = '\n';
    bufLenW = 5;
    writeUart();
    bufLenR = 0;

    ESP_ENABLE = 0;
    ESP_RESET = 0;
    ESP_BOOT = 0;    

    IP_EX |= bIP_USB; //boost USB interrupt priority
    GPIO_IE |= bIE_RXD0_LO; //enable UART0 RX interrupt
    IE_GPIO = 1; //enable GPIO interrupts
    EA = 1; //global interrupts enable

    //quick blink to siginfy (re)start
    LED = 0;
    mDelaymS(50);
    LED = 1;
    mDelaymS(50);
    LED = 0;
    mDelaymS(50);
    LED = 1;
    mDelaymS(50);
    LED = 0;
    mDelaymS(50);
    LED = 1;
    mDelaymS(50);
    LED = 0;


    while (1) {
        if (command == COMMAND_WRITE_UART) {
            command = 0;
            //this will block until the buffer is sent
            writeUart();
            inProgress = 0;
            LED = 0;
        } else
        if (command == COMMAND_SET_GPIO) {
            command = 0;
            setGpio();
        } else
        if (command == COMMAND_SET_BAUDR) {
            command = 0;
            bufLenR = 0; //scrap data from read buffer
            TR1 = 0; //Stop timer 1
            TI = 0;
            REN = 1; //Serial 0 receive diable
            mInitSTDIOBaud(data == 0 ? 74880 : 115200);
        }

        if (delayNonBlocking(200)) {
            LED = !LED;
        }
    }
}
