/* Master I2C driver, from project SuperSweet

    Copyright (c) 2020 Blinkinlabs
    
    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"), to deal
    in the Software without restriction, including without limitation the rights
    to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
    copies of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions:
    
    The above copyright notice and this permission notice shall be included in all
    copies or substantial portions of the Software.
    
    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
    OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
    SOFTWARE.
*/

#pragma once

#include <stdint.h>
#include <esp_err.h>

#define CONFIG_I2C_SDA_GPIO 18
#define CONFIG_I2C_SCL_GPIO 5
#define CONFIG_I2C_FREQ_KHZ 100

//! @defgroup master_i2c Master I2C driver
//!
//! @brief Thread-safe Master I2C driver
//!
//! This driver uses the I2C_NUM_1 peripheral on the ESP32. Functions are multi-
//! thread safe, protected by an internal semaphore.
//! @{

//! @brief Initialize the I2C bus in master mode
//!
esp_err_t master_i2c_init();

//! @brief Make a write transaction on the I2C bus
//!
//! @param[in] address Address of I2C bus to write to (7-bit address, right-aligned)
//! @param[in] reg I2C register to write to
//! @param[in] data Pointer to memory location to read data from
//! @param[in] size Number of bytes to write
esp_err_t master_i2c_write(uint8_t address, uint8_t reg, const uint8_t* data, uint32_t size);

//! @brief Make a read transaction on the I2C bus
//!
//! @param[in] address Address of I2C bus to read from(7-bit address, right-aligned)
//! @param[in] reg I2C register to read from
//! @param[out] data Pointer to memory location to write data to
//! @param[in] size Number of bytes to read
esp_err_t master_i2c_read(uint8_t address, uint8_t reg, uint8_t* data, uint32_t size);

//! @}
