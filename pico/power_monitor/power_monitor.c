#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/i2c.h"
#include "hardware/dma.h"

// LED for status indication
#define LED_PIN 25

// SPI Configuration
#define SPI_PORT spi0
#define PIN_RX 16
#define PIN_TX 19
#define PIN_SCK  18
#define PIN_CS   17

// INA226 I2C Configuration
#define I2C_PORT i2c1
#define SDA_PIN 2
#define SCL_PIN 3
#define I2C_FREQ 400000

// INA226 Default I2C Address (A1=GND, A0=GND)
#define INA226_ADDRESS 0x40

// INA226 Register Addresses
#define INA226_REG_CONFIG 0x00
#define INA226_REG_SHUNT_V 0x01
#define INA226_REG_BUS_V 0x02
#define INA226_REG_POWER 0x03
#define INA226_REG_CURRENT 0x04
#define INA226_REG_CALIBRATION 0x05
#define INA226_REG_MASK_ENABLE 0x06
#define INA226_REG_ALERT_LIMIT 0x07
#define INA226_REG_MANUF_ID 0xFE
#define INA226_REG_DIE_ID 0xFF

// Configuration values
#define INA226_CONFIG_RESET 0x8000
#define INA226_CONFIG_AVG_16 0x0400
#define INA226_CONFIG_VBUSCT_1100US 0x0100
#define INA226_CONFIG_VSHCT_1100US 0x0020
#define INA226_CONFIG_MODE_SHUNT_BUS_CONT 0x0007

// Constants for calculations
#define INA226_SHUNT_LSB_UV 2.5 // 2.5 µV per LSB
#define INA226_BUS_LSB_MV 1.25  // 1.25 mV per LSB

// Application-specific configuration
#define SHUNT_RESISTOR_OHMS 0.001 // 1mΩ shunt resistor
#define MAX_EXPECTED_CURRENT 1.0  // 1A maximum expected current

// Simple timing configuration
#define READ_INTERVAL_MS 5        // Read sensor every 5ms
#define SAMPLES_PER_TRANSMISSION 4 // 4 samples = 20ms = 50Hz
#define NUM_BUFFERS 2             // Simple double buffering

// Buffer configuration
#define SAMPLE_SIZE (sizeof(float) * 2 + 4)  // timestamp + current + voltage = 12 bytes
#define BUFFER_SIZE (SAMPLE_SIZE * SAMPLES_PER_TRANSMISSION) // 48 bytes per buffer
#define TRIGGER 17

// INA226 structure to hold sensor data
typedef struct
{
    float shunt_voltage_mv;
    float bus_voltage_v;
    float current_a;
    float power_w;
    uint16_t raw_shunt;
    uint16_t raw_bus;
    uint16_t raw_current;
    uint16_t raw_power;
} ina226_data_t;

// Simple buffer structure
typedef struct {
    uint8_t tx_data[BUFFER_SIZE];
    bool ready_to_send;
    bool being_transmitted;
} buffer_t;

// Vars for managing buffers
static buffer_t tx_buffers[NUM_BUFFERS];
static int write_buffer = 0;    // Index of buffer we're writing to (read from sensor)
static int read_buffer = 0;     // Index of buffer to transmitting to Pi

static int sample_count = 0;    // How many samples in current buffer
static volatile bool transmission_requested = false;
static volatile bool dma_busy = false;

static int data_chan;
static ina226_data_t sensor_data;

// Pack time data to buffer
void pack_data_time(uint8_t *buffer, int offset)
{
    uint32_t now_us = (uint32_t)time_us_64();
    buffer[offset + 0] = (now_us >> 24) & 0xFF;
    buffer[offset + 1] = (now_us >> 16) & 0xFF;
    buffer[offset + 2] = (now_us >> 8) & 0xFF;
    buffer[offset + 3] = (now_us >> 0) & 0xFF;
}

// Pack float-specific data to buffer
void pack_data_float(uint8_t *buffer, int offset, float data)
{
    uint8_t *p = (uint8_t *)&data;
    for (int i = 0; i < 4; i++) {
        buffer[offset + i] = p[i];
    }
}

// Pack all data to buffer (one sample)
void pack_sample(uint8_t *buffer, int sample_index)
{
    int offset = sample_index * SAMPLE_SIZE;
    pack_data_time(buffer, offset);
    pack_data_float(buffer, offset + 4, sensor_data.current_a);
    pack_data_float(buffer, offset + 8, sensor_data.bus_voltage_v);
}

// INA226 functions (unchanged)
bool ina226_write_register(uint8_t reg, uint16_t value)
{
    uint8_t buffer[3];
    buffer[0] = reg;
    buffer[1] = (value >> 8) & 0xFF;
    buffer[2] = value & 0xFF;
    int result = i2c_write_blocking(I2C_PORT, INA226_ADDRESS, buffer, 3, false);
    return (result == 3);
}

bool ina226_read_register(uint8_t reg, uint16_t *value)
{
    uint8_t buffer[2];
    int result = i2c_write_blocking(I2C_PORT, INA226_ADDRESS, &reg, 1, true); // Write data to initiate read
    if (result != 1) return false;
    
    result = i2c_read_blocking(I2C_PORT, INA226_ADDRESS, buffer, 2, false); // Read data populated into temporary buffer
    if (result != 2) return false;
    
    *value = (buffer[0] << 8) | buffer[1]; // Sensor value extracted from temporary buffer
    return true;
}

bool ina226_calibrate(void)
{
    float current_lsb = 0.001; // 1mA per LSB
    float cal_value = 0.00512 / (current_lsb * SHUNT_RESISTOR_OHMS);
    uint16_t calibration_reg = (uint16_t)(cal_value + 0.5);
    
    if (!ina226_write_register(INA226_REG_CALIBRATION, calibration_reg))
        return false;
        
    printf("INA226: Calibrated - Current LSB: %.3f mA\n", current_lsb * 1000);
    return true;
}

void ina226_init(void)
{
    uint16_t manuf_id, die_id;
    ina226_read_register(INA226_REG_MANUF_ID, &manuf_id);
    ina226_read_register(INA226_REG_DIE_ID, &die_id);
    
    ina226_write_register(INA226_REG_CONFIG, INA226_CONFIG_RESET);
    sleep_ms(100);
    
    uint16_t config = INA226_CONFIG_AVG_16 |
                      INA226_CONFIG_VBUSCT_1100US |
                      INA226_CONFIG_VSHCT_1100US |
                      INA226_CONFIG_MODE_SHUNT_BUS_CONT;
    
    ina226_write_register(INA226_REG_CONFIG, config);
    ina226_calibrate();
}

bool ina226_read_all_data(void)
{
    // Read from specified registers on INA226
    ina226_read_register(INA226_REG_SHUNT_V, &sensor_data.raw_shunt);
    ina226_read_register(INA226_REG_BUS_V, &sensor_data.raw_bus);
    ina226_read_register(INA226_REG_CURRENT, &sensor_data.raw_current);
    
    int16_t signed_shunt = (int16_t)sensor_data.raw_shunt;
    sensor_data.shunt_voltage_mv = signed_shunt * INA226_SHUNT_LSB_UV / 1000.0;
    
    sensor_data.bus_voltage_v = (sensor_data.raw_bus & 0x7FFF) * INA226_BUS_LSB_MV / 1000.0;
    
    int16_t signed_current = (int16_t)sensor_data.raw_current;
    sensor_data.current_a = 0.0126408 + (signed_current * 0.001) * 1.21342;
    
    return true;
}

void print_status(void)
{
    printf("I: %+7.3fA V: %6.3fV | Sample: %d/4 | WBuf: %d | RBuf: %d | DMA: %s\n", 
           sensor_data.current_a, sensor_data.bus_voltage_v, sample_count,
           write_buffer, read_buffer, dma_busy ? "BUSY" : "IDLE");
}

// Return transmitted data
uint8_t* create_data_sent(void)
{
    tx_buffers[write_buffer].ready_to_send = false;
    tx_buffers[write_buffer].being_transmitted = false;

    int8_t* data_ptr = tx_buffers[write_buffer].tx_data;
    write_buffer = (write_buffer + 1) % NUM_BUFFERS; // Switch to next buffer (:D flip-flop)
    sample_count = 0; // Reset sample count for new buffer
    
    return data_ptr;
}


void configure_dma()
{
    data_chan = dma_claim_unused_channel(true);
    dma_channel_config c = dma_channel_get_default_config(data_chan);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_8);
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, false);
    channel_config_set_dreq(&c, DREQ_SPI0_TX);

    dma_channel_configure(
        data_chan,
        &c,
        &spi_get_hw(SPI_PORT)->dr,
        create_data_sent(),        // Set dynamically
        BUFFER_SIZE, // Alwa; pointer of ys send full buffer
        false
    );
}

void start_transmission(int buffer_idx)
{
    if (dma_busy || !tx_buffers[buffer_idx].ready_to_send) return;
    
    tx_buffers[buffer_idx].being_transmitted = true;
    read_buffer = buffer_idx;
    dma_busy = true;
    
    gpio_set_function(PIN_TX, GPIO_FUNC_SPI);
    
    // Always send full buffer (4 samples = 48 bytes)
    dma_hw->ch[data_chan].read_addr = (uintptr_t)tx_buffers[buffer_idx].tx_data;
    dma_hw->ch[data_chan].transfer_count = BUFFER_SIZE;
    dma_start_channel_mask(1u << data_chan);
    
    printf("→ Started transmission of buffer %d (%d bytes)\n", buffer_idx, BUFFER_SIZE);
}

void handle_transmission_request(void)
{
    if (!transmission_requested || dma_busy) return;
    transmission_requested = false;
    
    // Find a ready buffer (prefer the one that's not being written to)
    for (int i = 0; i < NUM_BUFFERS; i++) {
        if (tx_buffers[i].ready_to_send && !tx_buffers[i].being_transmitted) {
            start_transmission(i);
            return;
        }
    }
    
    printf("⚠ No ready buffers for transmission\n");
}

void irq_handler(uint gpio, uint32_t events)
{
    if (events & GPIO_IRQ_EDGE_FALL) {
        transmission_requested = true;
    }
}

int initialize()
{
    stdio_init_all();

    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);
    gpio_put(LED_PIN, 1); 
    
    i2c_init(I2C_PORT, I2C_FREQ);
    gpio_set_function(SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(SDA_PIN);
    gpio_pull_up(SCL_PIN);

    spi_init(SPI_PORT, 1000 * 1000);  
    spi_set_slave(SPI_PORT, true);   
    gpio_set_function(PIN_RX, GPIO_FUNC_SPI);
    gpio_set_function(PIN_TX, GPIO_FUNC_SPI);
    gpio_set_function(PIN_SCK, GPIO_FUNC_SPI);
    // gpio_set_function(PIN_CS, GPIO_FUNC_SPI);  
    spi_set_format(spi0, 8, SPI_CPOL_0, SPI_CPHA_1, false);

    gpio_init(TRIGGER);
    gpio_set_dir(TRIGGER, GPIO_IN);
    gpio_set_irq_enabled_with_callback(TRIGGER, GPIO_IRQ_EDGE_FALL | GPIO_IRQ_EDGE_RISE, true, &irq_handler);

    ina226_init();
}

int main()
{
    initialize();
    configure_dma();
    while (true) {        
        ina226_read_all_data();
        pack_all_sensor_data();
    }
    return 0;
}