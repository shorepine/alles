/* Alles RevB power system driver

    Copyright (c) 2021 Blinkinlabs
    
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

#include <esp_err.h>
#include <stdint.h>
#include <stdbool.h>

typedef enum {
    POWER_SOURCE_WALL,                      //! Device is powered through the USB connector
    POWER_SOURCE_BATTERY,                   //! Device is powered by battery
} power_source_t;

typedef enum {
    POWER_CHARGE_STATUS_CHARGING,           //! Battery is discharging
    POWER_CHARGE_STATUS_CHARGED,            //! Battery is fully charged
    POWER_CHARGE_STATUS_DISCHARGING         //! Battery is charging
} power_charge_status_t;

typedef struct {
    power_source_t power_source;            //! Current power source
    power_charge_status_t charge_status;    //! Charging status
    uint8_t battery_percent;                //! TODO: Approximate battery charge left (0-100)
    uint32_t wall_voltage;                  //! Wall power voltage, in millivolts
    uint32_t battery_voltage;               //! Battery voltage, in millivolts
} power_status_t;

//! \brief Initialize the power subsystem
//!
//! \return ESP_OK on success
esp_err_t power_init();

//! \brief Control the 5v power supply
//!
//! \param enable If true, enable the power supply, otherwise disable
//! \return ESP_OK on success
esp_err_t power_5v_output_set(bool enable);

//! \brief Read the state of the power supply
//!
//! \param[out] power_status Description of the current power status
//! \return ESP_OK on success
esp_err_t power_read_status(power_status_t *power_status);

