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


#include "power.h"
#include "alles.h"
#include <driver/adc.h>
#include <esp_adc_cal.h>
#include <esp_log.h>

static const char TAG[] = "power";

#define DEFAULT_VREF    1100        //Use adc2_vref_to_gpio() to obtain a better estimate
#define NO_OF_SAMPLES   64          //Multisampling

static const adc_atten_t ADC_ATTEN = ADC_ATTEN_DB_0;
static const adc_unit_t ADC_UNIT = ADC_UNIT_1;
static const adc_bits_width_t ADC_BITS_WIDTH = ADC_WIDTH_BIT_12;

static esp_adc_cal_characteristics_t adc_chars;

static uint32_t read_adc1_multiple(adc_channel_t channel, int samples) {
    uint32_t adc_reading = 0;

    for (int i = 0; i < samples; i++)
        adc_reading += adc1_get_raw(channel);

    return adc_reading / samples;
}

static uint32_t read_adc1_channel(adc_channel_t channel) {
    
    const uint32_t adc_reading = read_adc1_multiple(channel, NO_OF_SAMPLES);
    const uint32_t val = esp_adc_cal_raw_to_voltage(adc_reading, &adc_chars);

    // Assume the resistor dividers have no error
    const uint32_t r_top = 100000;
    const uint32_t r_bottom = 10000;

    const uint32_t voltage = val * (r_top+r_bottom) / r_bottom;

    //ESP_LOGI(TAG, "channel=%i,adc_reading=%i,val=%i,voltage_mv=%i\n", channel, adc_reading, val, voltage);

    return voltage;
}

static uint8_t calc_charge_remaining(uint32_t battery_voltage) {
    // Crude charge status calculation
    // Battery range is 3.0 - 4.2V

    return 0;
}

esp_err_t power_read_status(power_status_t *power_status) {
    esp_err_t ret;

    const uint32_t wall_voltage = read_adc1_channel(WALL_SENSE_CHANNEL);

    // Enable battery sense network
    ret = gpio_set_level(BAT_SENSE_EN, 1);
    if(ret != ESP_OK)
        return ret;

    const uint32_t battery_voltage = read_adc1_channel(BATT_SENSE_CHANNEL);

    // Disable battery sense network
    ret = gpio_set_level(BAT_SENSE_EN, 0);
    if(ret != ESP_OK)
        return ret;

    const int charge_status = gpio_get_level(CHARGE_STAT);

    power_status->wall_voltage = wall_voltage;
    power_status->battery_voltage = battery_voltage;

    power_status->battery_percent = calc_charge_remaining(battery_voltage);

    if(wall_voltage > battery_voltage) {
        power_status->power_source = POWER_SOURCE_WALL;
        power_status->charge_status = ((charge_status == 0) ?
            POWER_CHARGE_STATUS_CHARGING : POWER_CHARGE_STATUS_CHARGED
            );
    }
    else {
        power_status->power_source = POWER_SOURCE_BATTERY;
        power_status->charge_status = POWER_CHARGE_STATUS_DISCHARGING;
    }

    return ESP_OK;
};

esp_err_t power_5v_output_set(bool enable) {
    return gpio_set_level(POWER_5V_EN, (enable ? 1 : 0));
}

esp_err_t power_init() {
    esp_err_t ret;

    // Initialize ADC. We expect it to use Vref mode, since all parts should have this cal from the factory.
    const esp_adc_cal_value_t val_type = esp_adc_cal_characterize(ADC_UNIT,
                                                                  ADC_ATTEN,
                                                                  ADC_BITS_WIDTH,
                                                                  DEFAULT_VREF,
                                                                  &adc_chars);

//    ESP_LOGI(TAG, "val_type=%i,adc_num=%i,atten=%i,bit_width=%i,coeff_a=%i,coeff_b=%i,vref=%i",
//        val_type,
//        adc_chars.adc_num,
//        adc_chars.atten,
//        adc_chars.bit_width,
//        adc_chars.coeff_a,
//        adc_chars.coeff_b,
//        adc_chars.vref);

    if(val_type != ESP_ADC_CAL_VAL_EFUSE_VREF)
        ESP_LOGE(TAG, "Warning: could not initialize ADC from eFuse Vref, analog readings may be inaccurate");

    adc1_config_width(ADC_BITS_WIDTH);
    adc1_config_channel_atten(BATT_SENSE_CHANNEL, ADC_ATTEN);
    adc1_config_channel_atten(WALL_SENSE_CHANNEL, ADC_ATTEN);

    {
        // Configure output GPIOs
        const gpio_config_t out_conf = {
            .mode = GPIO_MODE_OUTPUT,            //set as input mode
            .pin_bit_mask = (1ULL<<BAT_SENSE_EN)
                            | (1ULL<<POWER_5V_EN), //bit mask of the pins that you want to set,e.g.GPIO18/19
        };
        ret = gpio_config(&out_conf);                  //configure GPIO with the given settings
        if(ret != ESP_OK)
            return ret;

        power_5v_output_set(true);
    }

    {
        // Configure power status input as an input with pull-up
        const gpio_config_t in_conf = {
            .mode = GPIO_MODE_INPUT,            //set as input mode
            .pin_bit_mask = (1ULL << CHARGE_STAT),
            .pull_down_en = 0,                  //disable pull-down mode
            .pull_up_en = 1,                    //enable pull-up mode
        };
        ret = gpio_config(&in_conf);                  //configure GPIO with the given settings
        if(ret != ESP_OK)
            return ret;
    }

    {
        const gpio_config_t in_conf = {
            .mode = GPIO_MODE_INPUT,            //set as input mode
            .pin_bit_mask = (1ULL << 12),
            .pull_down_en = 0,                  //disable pull-down mode
            .pull_up_en = 0,                    //enable pull-up mode
        };
        ret = gpio_config(&in_conf);                  //configure GPIO with the given settings
        if(ret != ESP_OK)
            return ret;
    }



    return ESP_OK;
}
