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

#include "master_i2c.h"
#include <driver/i2c.h>
#include <freertos/FreeRTOS.h>
#include "blinkinlabs.h"

#define ACK_CHECK_EN 0x1 //!< I2C master will check ack from slave
#define ACK_CHECK_DIS 0x0 //!< I2C master will not check ack from slave

static SemaphoreHandle_t i2c_semaphore = NULL;

esp_err_t master_i2c_init()
{
    // init() shoud only be called once
    if(i2c_semaphore != NULL)
        return ESP_FAIL;

    i2c_semaphore = xSemaphoreCreateMutex();
    if(i2c_semaphore == NULL)
        return ESP_ERR_NO_MEM;

    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = CONFIG_I2C_SDA_GPIO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_io_num = CONFIG_I2C_SCL_GPIO,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = CONFIG_I2C_FREQ_KHZ * 1000,
    };

    esp_err_t ret;

    ret = i2c_param_config(I2C_NUM_1, &conf);
    if(ret != ESP_OK)
        return ret;

    ret = i2c_driver_install(I2C_NUM_1, conf.mode, 0, 0, 0);
    if(ret != ESP_OK)
        return ret;

    return ret;
}

esp_err_t master_i2c_write(uint8_t address, uint8_t reg, const uint8_t* data, uint32_t size)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    if(cmd == NULL)
        return ESP_ERR_NO_MEM;

    esp_err_t ret;

    ret = i2c_master_start(cmd);
    if(ret != ESP_OK)
        goto cmd_link_delete;

    ret = i2c_master_write_byte(cmd, address << 1 | I2C_MASTER_WRITE, ACK_CHECK_EN);
    if(ret != ESP_OK)
        goto cmd_link_delete;

    ret = i2c_master_write_byte(cmd, reg, ACK_CHECK_EN);
    if(ret != ESP_OK)
        goto cmd_link_delete;

    ret = i2c_master_write(cmd, data, size, ACK_CHECK_EN);
    if(ret != ESP_OK)
        goto cmd_link_delete;

    ret = i2c_master_stop(cmd);
    if(ret != ESP_OK)
        goto cmd_link_delete;

    if(xSemaphoreTake(i2c_semaphore, 1) != pdTRUE) {
        ret = ESP_ERR_TIMEOUT;
        goto cmd_link_delete;
    }

    ret = i2c_master_cmd_begin(I2C_NUM_1, cmd, 10);

    if(xSemaphoreGive(i2c_semaphore) != pdTRUE) {
        if(ret != ESP_OK)   // We can only report one error, so prioritize the first one
            ret = ESP_FAIL;
    }

cmd_link_delete:
    i2c_cmd_link_delete(cmd);

    return ret;
}

esp_err_t master_i2c_read(uint8_t address, uint8_t reg, uint8_t* data, uint32_t size)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    if(cmd == NULL)
        return ESP_ERR_NO_MEM;

    esp_err_t ret;
    ret = i2c_master_start(cmd);
    if(ret != ESP_OK)
        goto cmd_link_delete;

    ret = i2c_master_write_byte(cmd, address << 1 | I2C_MASTER_WRITE, ACK_CHECK_EN);
    if(ret != ESP_OK)
        goto cmd_link_delete;

    ret = i2c_master_write_byte(cmd, reg, ACK_CHECK_EN);
    if(ret != ESP_OK)
        goto cmd_link_delete;

    ret = i2c_master_start(cmd);
    if(ret != ESP_OK)
        goto cmd_link_delete;

    ret = i2c_master_write_byte(cmd, address << 1 | I2C_MASTER_READ, ACK_CHECK_EN);
    if(ret != ESP_OK)
        goto cmd_link_delete;

    ret = i2c_master_read(cmd, data, size, I2C_MASTER_LAST_NACK);
    if(ret != ESP_OK)
        goto cmd_link_delete;

    ret = i2c_master_stop(cmd);
    if(ret != ESP_OK)
        goto cmd_link_delete;

    if(xSemaphoreTake(i2c_semaphore, 0) != pdTRUE) {
        ret = ESP_ERR_TIMEOUT;
        goto cmd_link_delete;
    }

    ret = i2c_master_cmd_begin(I2C_NUM_1, cmd, 10);

    if(xSemaphoreGive(i2c_semaphore) != pdTRUE) {
        if(ret != ESP_OK)   // We can only report one error, so prioritize the first one
            ret = ESP_FAIL;
    }

cmd_link_delete:
    i2c_cmd_link_delete(cmd);

    return ret;
}
