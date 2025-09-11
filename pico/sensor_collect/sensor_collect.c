#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/i2c.h"
#include "hardware/uart.h"
#include "pico/binary_info.h"

#define PI_PORT i2c0
#define PI_SDA 0
#define PI_SCL 1
#define PICO_ADDR 0x42

#define ADC_PORT i2c1
#define ADC_SDA 2
#define ADC_SCL 3

#define ADC1_ADDR 0x48 
#define ADC2_ADDR 0x49 
const int8_t inputs[] = {0xC3, 0xD3, 0xE3, 0xF3};

#define REG_CONVERSION 0x00
#define REG_CONFIG     0x01

void initialize() {
    stdio_init_all();

    // I2C Initialisation. Using it at 400Khz.
    i2c_init(PI_PORT, 400*1000);
    gpio_set_function(PI_SDA, GPIO_FUNC_I2C);
    gpio_set_function(PI_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(PI_SDA);
    gpio_pull_up(PI_SCL);
    i2c_set_slave_mode(PI_PORT, true, PICO_ADDR);

    i2c_init(ADC_PORT, 400*1000);
    gpio_set_function(ADC_SDA, GPIO_FUNC_I2C);
    gpio_set_function(ADC_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(ADC_SDA);
    gpio_pull_up(ADC_SCL);
}

int16_t adc_read(uint8_t addr, uint8_t input) {
    uint8_t config_data[3];
    config_data[0] = REG_CONFIG;
    config_data[1] = input;
    config_data[2] = 0xE3;

    i2c_write_blocking(ADC_PORT, addr, config_data, 3, false);

    sleep_ms(2);  // Wait for conversion (860 SPS = ~1.2ms)

    uint8_t reg = REG_CONVERSION;
    uint8_t buffer[2];

    i2c_write_blocking(ADC_PORT, addr, &reg, 1, true);
    i2c_read_blocking(ADC_PORT, addr, buffer, 2, false);

    return (int16_t)((buffer[0] << 8) | buffer[1]);
}

int main()
{
    initialize();

    uint8_t buf[12];  // store incoming data
    
    while (true) {
        int16_t raw_values[6];
        raw_values[0] = adc_read(ADC1_ADDR, inputs[0]);
        raw_values[1] = adc_read(ADC1_ADDR, inputs[1]);
        // raw_values[2] = adc_read(ADC1_ADDR, inputs[2]);
        // raw_values[3] = adc_read(ADC1_ADDR, inputs[3]);
        // raw_values[4] = adc_read(ADC2_ADDR, inputs[0]);
        // raw_values[5] = adc_read(ADC2_ADDR, inputs[1]);

        // for (int i = 0; i < 6; i++) {
        //     buf[i*2] = (raw_values[i] >> 8) & 0xFF;  // High byte
        //     buf[i*2+1] = raw_values[i] & 0xFF;         // Low byte
        // }
        
        if (i2c_read_blocking(PI_PORT, PICO_ADDR, buf, 4, false)) {
            // Pack raw1 and raw2 into the buffer (4 bytes)
            buf[0] = (raw_values[0] >> 8) & 0xFF;  // raw1 High byte
            buf[1] = raw_values[0] & 0xFF;         // raw1 Low byte
            buf[2] = (raw_values[1] >> 8) & 0xFF;  // raw2 High byte
            buf[3] = raw_values[1] & 0xFF;         // raw2 Low byte

            // buf[0] = (raw2 >> 8) & 0xFF; // High byte
            // buf[1] = raw2 & 0xFF;        // Low byte
            i2c_write_blocking(PI_PORT, PICO_ADDR, buf, 4, false);
        }
    }
}