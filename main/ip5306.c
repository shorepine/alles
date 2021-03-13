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

#include "ip5306.h"
#include "master_i2c.h"
#include <stdio.h>
#include <stdint.h>

// Note: Register maps are taken from the datasheet, but may not work as described

// References:
// M5Stack driver: https://github.com/m5stack/M5Stack/blob/m5stack-fire/src/M5Stack.cpp
// https://github.com/m5stack/M5Stack/blob/master/src/utility/Power.cpp

#define BAT_I2C_ADDRESS 0x75

#define IP5306_SYS_CTL0     0x00
#define IP5306_SYS_CTL1     0x01
#define IP5306_SYS_CTL2     0x02
#define IP5306_CHARGER_CTL0 0x20
#define IP5306_CHARGER_CTL1 0x21
#define IP5306_CHARGER_CTL2 0x22
#define IP5306_CHARGER_CTL3 0x23
#define IP5306_CHG_DIG_CTL0 0x24
#define IP5306_REG_READ0    0x70
#define IP5306_REG_READ1    0x71
#define IP5306_REG_READ2    0x72
#define IP5306_REG_READ3    0x77
#define IP5306_REG_READ4    0x78    // Not in datasheet

typedef struct {
    uint8_t address;
    char * name;
} reg_name_t;

reg_name_t reg_names[] = {
    {.address=IP5306_SYS_CTL0,       .name="IP5306_SYS_CTL0",},
    {.address=IP5306_SYS_CTL1,       .name="IP5306_SYS_CTL1",},
    {.address=IP5306_SYS_CTL2,       .name="IP5306_SYS_CTL2",},
    {.address=IP5306_CHARGER_CTL0,   .name="IP5306_CHARGER_CTL0",},
    {.address=IP5306_CHARGER_CTL1,   .name="IP5306_CHARGER_CTL1",},
    {.address=IP5306_CHARGER_CTL2,   .name="IP5306_CHARGER_CTL2",},
    {.address=IP5306_CHARGER_CTL3,   .name="IP5306_CHARGER_CTL3",},
    {.address=IP5306_CHG_DIG_CTL0,   .name="IP5306_CHG_DIG_CTL0",},
    {.address=IP5306_REG_READ0,      .name="IP5306_REG_READ0",},
    {.address=IP5306_REG_READ1,      .name="IP5306_REG_READ1",},
    {.address=IP5306_REG_READ2,      .name="IP5306_REG_READ2",},
    {.address=IP5306_REG_READ3,      .name="IP5306_REG_READ3",},
    {.address=IP5306_REG_READ4,      .name="IP5306_REG_READ4",},
};

#define reg_name_count (sizeof(reg_names)/sizeof(reg_name_t))

// Note: All undefined bits are considered 'reserved', and the datasheet says their values should be preserved
// during any write operations

#define SYS_CTL0_BOOST_ENABLE   (1<<5)  // Enable DC-DC boost. This only takes effect after a power cycle via the power button. (works)
#define SYS_CTL0_CHARGER_ENABLE (1<<4)  // Enable battery charger (untested)
#define SYS_CTL0_LOAD_DETECT    (1<<2)  // Enable load detect feature (powers on automatically when a load is detected). (works)
#define SYS_CTL0_BOOST_NORMAL_OPEN  (1<<1)  // Disable light-load shutoff feature (turns power off when load falls below a threshold) (untested)
#define SYS_CTL0_BOOST_SD_ENABLE (1<<0) // Enable boost shutdown feature (allows the power button to turn off the boost circuit)

#define SYS_CTL1_BOOST_CONTROL  (1<<7)  // Disable boost via: 1: Long-press; 0: Two short presses
#define SYS_CTL1_WLED_CONTROL   (1<<6)  // Toggle flashlight via: 1: Two short presses; 0: Long press (untested)
#define SYS_CTL1_SHORT_PRESS    (1<<5)  // Disable boost via short press (works)
#define SYS_CTL1_BOOST_FALLOVER (1<<2)  // Turn the boost converter on automatically when VIN is disconnected (works)
#define SYS_CTL1_BAT_LOW_3V     (1<<0)  // Automatically shut down when battery voltage <3V (works)

#define SYS_CTL2_KEY_DELAY      (1<<4)  // Long-press duration: 0: 2s; 1: 3s (works)
#define SYS_CTL2_LOAD_TIMEOUT   (1<<2)  // (2 bit) 0: 8s delay; 1: 32s delay; 2: 16s delay; 3: 64s delay (doesn't work?)

#define CHARGER_CTL0_CHARGE_STOP (1<<0) // (2 bit) 0: 4.14/4.26/4.305/4.35; 1: 4.17/4.275/4.32/4.365;
                                        // 2: 4.185/4.29/4.335/4.38; 3: 4.2/4.305/4.35/4.395

#define CHARGER_CTL1_CHARGE_DONE_CURRENT (1<<6) // (2 bit): 0: 200mA; 1: 400mA; 2: 500mA; 3: 600mA
#define CHARGER_CTL1_VOUT_UNDERVOLT (1<<2) // (3 bit): 0: 4.45V; 1: 4.5V; 2: 4.55V; 3: 4.6V;
                                           // 4: 4.65V; 5: 4.7V; 6: 4.75V; 7: 4.8V

#define CHARGER_CTL2_BATTERY_VOLTAGE (1<<2) // (2 bit) 0: 4.2V; 1: 4.3V; 2: 4.35V; 3: 4.4V
#define CHARGER_CTL2_CONST_VOLT_LEVEL (1<<0) // (2 bit) 0: disable; 1: 14mV; 2: 28mV; 3: 42mV

#define CHARGER_CTL3_CONST_CURRENT_LOOP (1<<5) // Constant current monitor at: 1: VIN; 2: Battery

#define REG_READ0_CHARGE_EN     (1<<3)  // Charger enable: 1: Enabled; 0: Disabled (works)

#define REG_READ1_CHARGE_DONE   (1<<3)  // Charger state: 1: Fully charged; 0: Charging (works)

#define REG_READ2_LIGHT_LOAD    (1<<2)  // Output load detection: 1: Light; 0: Heavy (untested)

#define REG_READ3_BUTTON_DOWN_TIMER   (1<<7)  // (not in datasheet) Normally high, pulses low periodically while button pressed
#define REG_READ3_BUTTON_DOWN   (1<<6)  // (not in datasheet) Current button state: 1: pressed 0: not pressed
#define REG_READ3_DOUBLE_PRESS  (1<<2)  // 1: Double press detected, write 1 to clear (doesn't work)
#define REG_READ3_LONG_PRESS    (1<<1)  // 1: Long press detected, write 1 to clear (works)
#define REG_READ3_SHORT_PRESS   (1<<0)  // 1: Short press detected, write 1 to clear (works)

// REG_READ_4 not in datasheet, but values have been reverse engineered.

static esp_err_t clear_bits(uint8_t reg, uint8_t bits) {
    uint8_t val;
    esp_err_t ret;

    ret = master_i2c_read(BAT_I2C_ADDRESS, reg, &val, 1);
    if(ret != ESP_OK)
        return ret;

    val &= ~(bits);

    ret = master_i2c_write(BAT_I2C_ADDRESS, reg, &val, 1);
    if(ret != ESP_OK)
        return ret;

    return ESP_OK;
}

static esp_err_t set_bits(uint8_t reg, uint8_t bits) {
    uint8_t val;
    esp_err_t ret;

    ret = master_i2c_read(BAT_I2C_ADDRESS, reg, &val, 1);
    if(ret != ESP_OK)
        return ret;

    val |= (bits);

    ret = master_i2c_write(BAT_I2C_ADDRESS, reg, &val, 1);
    if(ret != ESP_OK)
        return ret;

    return ESP_OK;
}


esp_err_t ip5306_charge_state_get(ip5306_charge_state_t *charge_state) {
    if(charge_state == NULL)
        return ESP_FAIL;

    // From the datasheet:
    // 2) Use bit3 of register address=0x70 to judge whether IP5306 is charging or discharging: bit3=1 Charge, discharge when bit3=0
    // 3) Use bit3 of register address=0x71 to judge whether the battery is fully charged: full when bit3=1, not full when bit3=0

    // From experimentation:
    // If bits 3:0 of register 0x78 are 0x0F, battery voltage is critically low

    uint8_t val;
    esp_err_t ret;

    ret = master_i2c_read(BAT_I2C_ADDRESS, IP5306_REG_READ0, &val, 1);
    if(ret != ESP_OK)
        return ret;

    if(!(val & REG_READ0_CHARGE_EN)) {
        ip5306_battery_voltage_t battery_voltage;
        ret = ip5306_battery_voltage_get(&battery_voltage);
        if(ret != ESP_OK)
            return ret;

        if(battery_voltage == BATTERY_UNDER_33) {
            *charge_state = CHARGE_STATE_DISCHARGING_LOW_BAT;
            return ESP_OK;
        }
        else {
            *charge_state = CHARGE_STATE_DISCHARGING;
            return ESP_OK;
        }
    }

    ret = master_i2c_read(BAT_I2C_ADDRESS, IP5306_REG_READ1, &val, 1);
    if(ret != ESP_OK)
        return ret;

    if(val & REG_READ1_CHARGE_DONE) {
         *charge_state = CHARGE_STATE_CHARGED;
         return ESP_OK;
    }

    *charge_state = CHARGE_STATE_CHARGING;
    return ESP_OK;
}

//void ip5306_check_reg_changes() {
//    static uint8_t regs[256];
//    static bool firstrun = true;
//    uint8_t val;
//    esp_err_t ret;
//
//// Note: There appear to be more registers available than what the datasheet would suggest
//    for(int i = 0; i < 256; i++) {
//        ret = master_i2c_read(BAT_I2C_ADDRESS, i, &val, 1);
//        if(ret != ESP_OK) {
//            printf("%02x: (error reading, error:%s)\n", i, esp_err_to_name(ret));
//            continue;
//        }
//
//        if(!firstrun && (regs[i] != val))
//            printf("Change: 0x%02x, was:%02x now:0x%02x\n", i, regs[i], val);
//        
//        if(firstrun)
//            printf("Initial: 0x%02x: 0x%02x\n", i, val);
//
//        regs[i] = val;
//    }
//
//    firstrun = false;
//}

void ip5306_dump_regs() {
    uint8_t val = 0;
    esp_err_t ret;

    for(int i = 0; i < reg_name_count; i++) {
        ret = master_i2c_read(BAT_I2C_ADDRESS, reg_names[i].address, &val, 1);
        if(ret != ESP_OK) {
            printf("%s: (error reading, error:%s)\n", reg_names[i].name, esp_err_to_name(ret));
            continue;
        }

        printf("%s: 0x%02x\n", reg_names[i].name, val);
    }

//    for(int i = 0; i < 128; i++) {
//        ret = master_i2c_read(BAT_I2C_ADDRESS, i, &val, 1);
//        if(ret != ESP_OK) {
//            printf("0x%02x: (error reading, error:%s)\n", i, esp_err_to_name(ret));
//            continue;
//        }
//
//        printf("0x%02x: 0x%02x\n", i, val);
//    }
}

esp_err_t ip5306_button_press_get(int *button_press) {
    if(button_press == NULL)
        return ESP_FAIL;

    uint8_t val;
    esp_err_t ret;
    ret = master_i2c_read(BAT_I2C_ADDRESS, IP5306_REG_READ3, &val, 1);
    if(ret != ESP_OK)
        return ret;

    // Mask only the button press fields
    val = val & (REG_READ3_DOUBLE_PRESS | REG_READ3_LONG_PRESS | REG_READ3_SHORT_PRESS);

    // Write the button states back, to clear them
    ret = master_i2c_write(BAT_I2C_ADDRESS, IP5306_REG_READ3, &val, 1);
    if(ret != ESP_OK)
        return ret;

    *button_press = 0;
    if(val & REG_READ3_DOUBLE_PRESS) // Doesn't appear to work
        *button_press |= BUTTON_DOUBLE_PRESS;
    if(val & REG_READ3_LONG_PRESS)
        *button_press |= BUTTON_LONG_PRESS;
    if(val & REG_READ3_SHORT_PRESS)
        *button_press |= BUTTON_SHORT_PRESS;

    return ESP_OK;
}

esp_err_t ip5306_battery_voltage_get(ip5306_battery_voltage_t *battery_voltage) {
    if(battery_voltage == NULL)
        return ESP_FAIL;

    uint8_t val;
    esp_err_t ret;

    ret = master_i2c_read(BAT_I2C_ADDRESS, IP5306_REG_READ4, &val, 1);
    if(ret != ESP_OK)
        return ret;

    val = (val & 0x0f);
    switch((ip5306_battery_voltage_t)(val)) {
    case BATTERY_OVER_395:
    case BATTERY_38_395:
    case BATTERY_36_38:
    case BATTERY_33_36:
    case BATTERY_UNDER_33:
        *battery_voltage = val;
        return ESP_OK;
    default:
        // If there was an error, assume the battery is critical
        return ESP_FAIL;
    };
}

//// An attempt to probe for undocumented registers
//void ip5306_try_write_regs() {
//    return;
//
//    for(int i = 0x03; i < 0x90; i++) {
//        if((i == 0x12)
//            | (i == 0x20)
//            | (i == 0x21)
//            | (i == 0x22)
//            | (i == 0x23)
//            | (i == 0x24)
//            | (i == 0x35)
//            | (i == 0x40)
//            | (i == 0x77)
//            | (i == 0x2D)
//            ) {
//            continue;
//        }
//
//        set_bits(i, 0xFF);
//    }
//
//    /*
//    for(int i = 0x3; i < 0x16; i++) {
//        uint8_t original_val;
//        ret = master_i2c_read(BAT_I2C_ADDRESS, i, &original_val, 1);
//        if(ret != ESP_OK) {
//            printf("%02x: (error reading, error:%s)\n", i, esp_err_to_name(ret));
//            continue;
//        }
//
//        const uint8_t write_val = ~original_val;
//        ret = master_i2c_write(BAT_I2C_ADDRESS, i, &write_val, 1);
//        if(ret != ESP_OK) {
//            printf("%02x: (error reading, error:%s)\n", i, esp_err_to_name(ret));
//            continue;
//        }
//
//        uint8_t read_val;
//        ret = master_i2c_read(BAT_I2C_ADDRESS, i, &read_val, 1);
//        if(ret != ESP_OK) {
//            printf("%02x: (error reading, error:%s)\n", i, esp_err_to_name(ret));
//            continue;
//        }
//
//        if(original_val != read_val)
//            printf("Write success: 0x%02x, was:0x%02x now:0x%02x\n", i, original_val, read_val);
//        else
//            printf("Write failure: 0x%02x, was:0x%02x now:0x%02x\n", i, original_val, read_val);
//    }
//    */
//}

esp_err_t ip5306_auto_poweroff_enable() {
    esp_err_t ret;

    // First, change the power button setting so that a single press will turn the power off.
    // This is just to make it a little easier if you try to turn the power back on during the time
    // that the ESP32 is in deep sleep mode but the IP5306 hasn't turned off yet.
    ret = set_bits(IP5306_SYS_CTL1, SYS_CTL1_SHORT_PRESS);
    if(ret != ESP_OK)
        return ret;

    // Enable the light-load detect function, which will allow the board to turn off after a fixed
    // time-out.
    ret = clear_bits(IP5306_SYS_CTL0, SYS_CTL0_BOOST_NORMAL_OPEN);
    if(ret != ESP_OK)
        return ret;

    return ESP_OK;
}

esp_err_t ip5306_init() {
    esp_err_t ret;

    // LOAD_DETECT: Disable auto-on when a load is detected
    // Note: Disabling this feature disables the boost converter in standby mode, which reduces
    // the power-off drain current from 600uA to 33uA. This is critical to achieving any sort of
    // standby time.
    ret = clear_bits(IP5306_SYS_CTL0,
        SYS_CTL0_LOAD_DETECT
        );
    if(ret != ESP_OK)
        return ret;

    ret = set_bits(IP5306_SYS_CTL0,
        SYS_CTL0_BOOST_ENABLE
        | SYS_CTL0_CHARGER_ENABLE
        | SYS_CTL0_BOOST_SD_ENABLE
        | SYS_CTL0_BOOST_NORMAL_OPEN
        );
    if(ret != ESP_OK)
        return ret;

    ret = set_bits(IP5306_SYS_CTL1,
        SYS_CTL1_BOOST_FALLOVER // Prevent a low-load condition from turning off the IP5306
        );

    ret = clear_bits(IP5306_SYS_CTL1,
        SYS_CTL1_SHORT_PRESS
        | SYS_CTL1_BOOST_CONTROL
        | SYS_CTL1_WLED_CONTROL
        );

    ret = clear_bits(IP5306_SYS_CTL2,
        SYS_CTL2_KEY_DELAY
        | 0x0C);    // Set auto-off delay to 8 seconds
    if(ret != ESP_OK)
        return ret;

    // Clear any residual button presses
    int button_state;
    ret = ip5306_button_press_get(&button_state);
    if(ret != ESP_OK)
        return ret;

    return ESP_OK;
}
