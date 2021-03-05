/* IP5306-I2C driver

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

#include <stdbool.h>
#include <stdint.h>
#include <esp_err.h>

//! @defgroup ip5306 IP5306 driver
//!
//! @brief Driver for the IP5306 battery charge management IC
//!
//! @{

typedef enum {
    CHARGE_STATE_CHARGED,               //!< External power connected, battery charged
    CHARGE_STATE_CHARGING,              //!< External power connected, battery charging
    CHARGE_STATE_DISCHARGING,           //!< External power disconnected, battery discharging
    CHARGE_STATE_DISCHARGING_LOW_BAT,   //!< External power disconnected, battery discharging and under 3.3V
} ip5306_charge_state_t;

void ip5306_check_reg_changes();
void ip5306_try_write_regs();

//! @brief Get the charge state
//!
//! @return Current charge state
esp_err_t ip5306_charge_state_get(ip5306_charge_state_t *charge_state);

//! @brief Diagnostic: dump all register values
//!
//! Read the value of all registers, and dump to serial using printf
void ip5306_dump_regs();

typedef enum {
    BUTTON_DOUBLE_PRESS = (1<<0),   //!< Power button was double-clicked
    BUTTON_LONG_PRESS = (1<<1),     //!< Power button was held down for 2-3 seconds
    BUTTON_SHORT_PRESS = (1<<2),    //!< Power button was single-clicked
} ip5306_button_press_t;

//! @brief Get a bitfield of any power button presses
//!
//! This function also clears the button press flags.
//!
//! Note: The IP5306 does not report events for the button press that is assigned to boost control.
//!       By default, this is BUTTON_DOUBLE_PRESS
//! 
//! @return A combination of ip5306_button_press_t entries
esp_err_t ip5306_button_press_get(int *button_press);

typedef enum {
    BATTERY_OVER_395 = 0x00,        //!< Battery voltage over 3.95V (full)
    BATTERY_38_395 = 0x08,          //!< Battery voltage between 3.8V and 3.95V (good)
    BATTERY_36_38 = 0x0C,           //!< Battery voltage between 3.6V and 3.8V (ok)
    BATTERY_33_36 = 0x0E,           //!< Battery voltage betwwen 3.3V and 3.6V (low)
    BATTERY_UNDER_33 = 0x0F,        //!< Battery voltage under 3.3V (critically low)
} ip5306_battery_voltage_t;

//! @brief Get a measurement of the battery voltage level
//!
//! Get a measurement of the battery voltage. This can be approximately coorelated to the
//! remaining charge in the battery. This will work most reliably if the battery is kept
//! under a steady-state load that does not exceed it's rated discharge current.
//!
//! @return Current battery voltage
esp_err_t ip5306_battery_voltage_get(ip5306_battery_voltage_t *battery_voltage);


esp_err_t ip5306_auto_poweroff_enable();

esp_err_t ip5306_init();
//! @}
