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

#pragma once

#include <stdint.h>

#ifdef MINGW
#include <libusbx-1.0/libusb.h>
#else
#include <libusb-1.0/libusb.h>
#endif

typedef struct {
    libusb_context* c;
    libusb_device_handle *h;
    uint32_t baudrate;
} loader_usb_config_t;

esp_loader_error_t loader_port_usb_init(loader_usb_config_t *config);
