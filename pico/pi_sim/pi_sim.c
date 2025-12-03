// import standard libraries, pico c sdk, and spi hardware libraries
#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"

// LED for status indication
#define LED_PIN 25

// SPI Defines; creating macros
// We are going to use SPI 0, and allocate it to the following GPIO pins
// Pins can be changed, see the GPIO function select table in the datasheet for information on GPIO assignments
#define SPI_PORT spi0
#define PIN_MISO 16
#define PIN_CS 17
#define PIN_SCK 18
#define PIN_MOSI 19

#define BUFFER_SIZE 48      // must match transmitter buffer size
#define READ_INTERVAL_MS 20 // how often to poll data
uint8_t rx_buffer[BUFFER_SIZE];

void spi_master_init()
{
    // initialize spi configuration

    // we need to initialize the GPIO functions of the pico to
    // configure for SPI hardware usage; essentially changing registers
    // and set clock frequency
    spi_init(SPI_PORT, 1000 * 1000); // 1 MHz clk
    gpio_set_function(PIN_SCK, GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);
    gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);
    gpio_set_function(PIN_CS, GPIO_FUNC_SIO);

    // configure the chip select pin
    gpio_init(PIN_CS);
    gpio_set_dir(PIN_CS, GPIO_OUT);
    gpio_put(PIN_CS, 1); // sets to inactive (high)

    // configure the LED pin
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);
    gpio_put(LED_PIN, 1);
    printf("SPI Master initialized.\n");
}

void spi_read_data()
{
    gpio_put(PIN_CS, 0); // active low
    sleep_us(5);         // settle time

    // Write BUFFER_SIZE bytes from src (dummy_tx) to SPI (TX data register)
    // simm. read BUFFER_SIZE bytes from SPI (RX data register) to dst (rx_buffer)
    // sample size of 48 bytes
    uint8_t dummy_tx[BUFFER_SIZE] = {0};
    spi_write_read_blocking(SPI_PORT, dummy_tx, rx_buffer, BUFFER_SIZE);
    gpio_put(PIN_CS, 1); // inactive high

    printf("Received %d bytes:\n", BUFFER_SIZE);
    uint16_t start_b = 0;
    uint16_t end_b = 0;
    const char* validity = NULL;

    start_b = (rx_buffer[0] << 8) | rx_buffer[1];
    end_b = (rx_buffer[BUFFER_SIZE - 2] << 8) | rx_buffer[BUFFER_SIZE - 1];
    validity = (start_b == 0xABCD && end_b == 0xDCBA) ? "valid" : "invalid";

    printf("Start bytes: %04X\n", start_b);
    printf("End bytes:   %04X\n\n", end_b);
    printf("The following buffer is %s:\n", validity);

    for (int i = 2; i < BUFFER_SIZE-2; i++)
    {
        printf("%02X ", rx_buffer[i]);
        if ((i - 1) % 12 == 0)
            printf("\n");
    }
    printf("\n");
}

int main()
{
    stdio_init_all();
    spi_master_init();
    sleep_ms(2000); // allow transmitter setup

    while (true)
    {
        spi_read_data();
        sleep_ms(READ_INTERVAL_MS);

        printf("Hello, world!\n");
        sleep_ms(1000);
    }
    return 0;
}
