#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/i2c.h"
#include "hardware/uart.h"
#include "pico/binary_info.h"

// SPI Defines
#define SPI_PORT spi0
#define PIN_MISO 16
#define PIN_CS   17
#define PIN_SCK  18
#define PIN_MOSI 19

// I2C defines
// This example will use I2C0 on GPIO8 (SDA) and GPIO9 (SCL) running at 400KHz.
#define I2C_PORT i2c0
#define I2C_SDA 8
#define I2C_SCL 9
#define ADS1115_ADDR 0x48 
#define REG_CONVERSION 0x00
#define REG_CONFIG     0x01

// UART defines
#define UART_ID uart0
#define BAUD_RATE 115200
#define UART_TX_PIN 0
#define UART_RX_PIN 1

void initialize() {
    stdio_init_all();

    // SPI initialisation. This example will use SPI at 1MHz.
    spi_init(SPI_PORT, 1000*1000);
    gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);
    gpio_set_function(PIN_CS,   GPIO_FUNC_SIO);
    gpio_set_function(PIN_SCK,  GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);
    
    // Chip select is active-low, so we'll initialise it to a driven-high state
    gpio_set_dir(PIN_CS, GPIO_OUT);
    gpio_put(PIN_CS, 1);

    // I2C Initialisation. Using it at 400Khz.
    i2c_init(I2C_PORT, 400*1000);
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA);
    gpio_pull_up(I2C_SCL);

    // Set up our UART
    uart_init(UART_ID, BAUD_RATE);
    gpio_set_function(UART_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(UART_RX_PIN, GPIO_FUNC_UART);
}

void ads1115_start_conversion() {
    uint8_t config_data[3];

    config_data[0] = REG_CONFIG;

    // 0xC3E3 = single-shot, AIN0, ±4.096V, 860SPS
    config_data[1] = 0xC3;
    config_data[2] = 0xE3;

    i2c_write_blocking(I2C_PORT, ADS1115_ADDR, config_data, 3, false);
}

int16_t ads1115_read_conversion() {
    uint8_t reg = REG_CONVERSION;
    uint8_t buffer[2];

    i2c_write_blocking(I2C_PORT, ADS1115_ADDR, &reg, 1, true);
    i2c_read_blocking(I2C_PORT, ADS1115_ADDR, buffer, 2, false);

    return (int16_t)((buffer[0] << 8) | buffer[1]);
}

void uart_print_voltage(int16_t raw) {
    float voltage = (raw * 4.096f) / 32768.0f;

    char buf[64];
    // snprintf(buf, sizeof(buf), "Raw ADC: %d, Voltage: %.4f V\r\n", raw, voltage);
    snprintf(buf, sizeof(buf), "Raw ADC: %d, Voltage: %.4f V\r\n", raw, voltage);
    uart_puts(UART_ID, buf);
}

void print_float(float raw) {
    float voltage = (raw * 4.096f) / 32768.0f;
    char buffer[32];  
    sprintf(buffer, "%f\n", voltage); 
    uart_puts(UART_ID, buffer);  
}

int main()
{
    initialize();

    uint8_t reg = 0x00;
    uint8_t data;
    
    while (true) {
        ads1115_start_conversion();
        sleep_ms(2);  // Wait for conversion (860 SPS = ~1.2ms)
        int16_t raw = ads1115_read_conversion();

        // uart_print_voltage(raw);
        print_float(raw);
        sleep_us(200);
    }
}