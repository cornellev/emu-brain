#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/adc.h"
#include "hardware/dma.h"

#define LED 25

#define SPI_PORT spi0
#define PIN_RX 16
#define PIN_TX 19
#define PIN_SCK  18
#define PIN_CS   17

#define LEN 11
#define TRIGGER 0

uint8_t tx_data[LEN]; 
int data_chan ;

void set_gpio_hi_z(uint pin) {
    io_bank0_hw->io[pin].ctrl = (io_bank0_hw->io[pin].ctrl & ~IO_BANK0_GPIO0_CTRL_OEOVER_BITS) | (IO_BANK0_GPIO0_CTRL_OEOVER_VALUE_DISABLE << IO_BANK0_GPIO0_CTRL_OEOVER_LSB);
}

void restore_gpio_oe_control(uint pin) {
    io_bank0_hw->io[pin].ctrl = (io_bank0_hw->io[pin].ctrl & ~IO_BANK0_GPIO0_CTRL_OEOVER_BITS) | (IO_BANK0_GPIO0_CTRL_OEOVER_VALUE_NORMAL << IO_BANK0_GPIO0_CTRL_OEOVER_LSB);
}

uint16_t read_adc(uint input) {
    adc_select_input(input);
    return adc_read();  
}

void configure_dma() {
    data_chan = dma_claim_unused_channel(true);
    dma_channel_config c = dma_channel_get_default_config(data_chan);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_8);            // 8-bit txfers
    channel_config_set_read_increment(&c, true);                       // yes read incrementing
    channel_config_set_write_increment(&c, false);                     // no write incrementing
    channel_config_set_dreq(&c, DREQ_SPI0_TX);

    dma_channel_configure(
        data_chan,                  // Channel to be configured
        &c,                         // The configuration we just created
        &spi_get_hw(SPI_PORT)->dr,  // write address (SPI data register)
        tx_data,                    // The initial read address
        LEN,                        // Number of transfers
        false                       // Don't start immediately.
    );
}

void irq_handler(uint gpio, uint32_t events) {
    if (events & GPIO_IRQ_EDGE_FALL) {

        gpio_set_function(PIN_TX, GPIO_FUNC_SPI);
        
        uint32_t now_us = (uint32_t)time_us_64();
        uint16_t adc0 = read_adc(0);  
        uint16_t adc1 = read_adc(1);  
        uint16_t adc2 = read_adc(2);  

        tx_data[0] = (now_us >> 24) & 0xFF;
        tx_data[1] = (now_us >> 16) & 0xFF;
        tx_data[2] = (now_us >> 8) & 0xFF;
        tx_data[3] = (now_us >> 0) & 0xFF;

        tx_data[4] = (adc0 >> 8) & 0xFF;
        tx_data[5] = (adc0 >> 0) & 0xFF;

        tx_data[6] = (adc1 >> 8) & 0xFF;
        tx_data[7] = (adc1 >> 0) & 0xFF;

        tx_data[8] = (adc2 >> 8) & 0xFF;
        tx_data[9] = (adc2 >> 0) & 0xFF;

        tx_data[10] = 0x00; // End byte

        dma_hw->ch[data_chan].read_addr = (uintptr_t)tx_data;
        dma_start_channel_mask(1u << data_chan);

        
        printf("\nSent: ");
        for (int i = 0; i < LEN; i++) {
            printf("%02X ", tx_data[i]);
        }

    } else if (events & GPIO_IRQ_EDGE_RISE) {
        // printf("here\n");
        set_gpio_hi_z(PIN_TX);
    }
}

void initialize() {
    stdio_init_all();

    gpio_init(LED);
    gpio_set_dir(LED, GPIO_OUT);
    gpio_put(LED, 1);

    gpio_init(TRIGGER);
    gpio_set_dir(TRIGGER, GPIO_IN);
    gpio_set_irq_enabled_with_callback(TRIGGER, GPIO_IRQ_EDGE_FALL | GPIO_IRQ_EDGE_RISE, true, &irq_handler);

    adc_init();
    adc_gpio_init(26);  // GPIO 26
    adc_gpio_init(27);  // GPIO 27
    adc_gpio_init(28);  // GPIO 28

    spi_init(SPI_PORT, 1000 * 1000);  
    spi_set_slave(SPI_PORT, true);   
    gpio_set_function(PIN_RX, GPIO_FUNC_SPI);
    gpio_set_function(PIN_TX, GPIO_FUNC_SPI);
    gpio_set_function(PIN_SCK, GPIO_FUNC_SPI);
    gpio_set_function(PIN_CS, GPIO_FUNC_SPI);  
    spi_set_format(spi0, 8, SPI_CPOL_0, SPI_CPHA_1, false);
}

int main() {
    initialize();
    configure_dma();
}