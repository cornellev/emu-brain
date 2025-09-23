#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "hardware/dma.h"

// LED for status indication
#define LED_PIN 25

// INA226 I2C Configuration
#define INA226_I2C_PORT i2c1
#define INA226_SDA_PIN 2
#define INA226_SCL_PIN 3
#define INA226_I2C_FREQ 400000

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
#define SHUNT_RESISTOR_OHMS 0.001 // 1mΩ shunt resistor (adjust as needed)
#define MAX_EXPECTED_CURRENT 1.0  // 1A maximum expected current (adjust as needed)

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

// For TX Buffer
#define LEN sizeof(ina226_data_t)
#define TRIGGER 0

uint8_t tx_data[LEN];
int data_chan;
ina226_data_t sensor_data;

// Packing data into buffer
void pack_data_time(int start_index)
{
    uint32_t now_us = (uint32_t)time_us_64();
    if (start_index + 4 > LEN)
        return; // Prevent overflow
    tx_data[start_index] = (now_us >> 24) & 0xFF;
    tx_data[start_index + 1] = (now_us >> 16) & 0xFF;
    tx_data[start_index + 2] = (now_us >> 8) & 0xFF;
    tx_data[start_index + 3] = (now_us >> 0) & 0xFF;
}

void pack_data_float(int start_index, float data)
{
    if (start_index + 4 > LEN)
        return; // Prevent overflow
    uint8_t *p = (uint8_t *)&data;
    for (int i = 0; i < 4; i++)
    {
        tx_data[start_index + i] = p[i];
    }
}

void pack_data_uint16(int start_index, uint16_t data)
{
    if (start_index + 2 > LEN)
        return;                                // Prevent overflow
    tx_data[start_index] = (data >> 8) & 0xFF; // MSB
    tx_data[start_index + 1] = data & 0xFF;    // LSB
}

void pack_all_sensor_data(void)
{
    // Pack the data into the transmission buffer
    pack_data_time(0);
    pack_data_float(4, sensor_data.shunt_voltage_mv);
    pack_data_float(8, sensor_data.bus_voltage_v);
    pack_data_float(12, sensor_data.current_a);
    pack_data_float(16, sensor_data.power_w);
    pack_data_uint16(20, sensor_data.raw_shunt);
    pack_data_uint16(22, sensor_data.raw_bus);
    pack_data_uint16(24, sensor_data.raw_current);
    pack_data_uint16(26, sensor_data.raw_power);
}

// Function prototypes
bool ina226_write_register(uint8_t reg, uint16_t value);
bool ina226_read_register(uint8_t reg, uint16_t *value);
bool ina226_init(void);
bool ina226_configure(void);
bool ina226_calibrate(void);
bool ina226_read_all_data(void);
void ina226_print_data(void);
void blink_led(int times);

void blink_led(int times)
{
    for (int i = 0; i < times; i++)
    {
        gpio_put(LED_PIN, 1);
        sleep_ms(100);
        gpio_put(LED_PIN, 0);
        sleep_ms(100);
    }
}

bool ina226_write_register(uint8_t reg, uint16_t value)
{
    uint8_t buffer[3];
    buffer[0] = reg;
    buffer[1] = (value >> 8) & 0xFF; // MSB first
    buffer[2] = value & 0xFF;        // LSB second

    int result = i2c_write_blocking(INA226_I2C_PORT, INA226_ADDRESS, buffer, 3, false);
    return (result == 3);
}

bool ina226_read_register(uint8_t reg, uint16_t *value)
{
    uint8_t buffer[2];

    // Write register address
    int result = i2c_write_blocking(INA226_I2C_PORT, INA226_ADDRESS, &reg, 1, true);
    if (result != 1)
    {
        return false;
    }

    // Read 2 bytes
    result = i2c_read_blocking(INA226_I2C_PORT, INA226_ADDRESS, buffer, 2, false);
    if (result != 2)
    {
        return false;
    }

    // Combine bytes (MSB first)
    *value = (buffer[0] << 8) | buffer[1];
    return true;
}

bool ina226_init(void)
{
    // Initialize I2C
    i2c_init(INA226_I2C_PORT, INA226_I2C_FREQ);
    gpio_set_function(INA226_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(INA226_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(INA226_SDA_PIN);
    gpio_pull_up(INA226_SCL_PIN);

    sleep_ms(100);

    printf("INA226: I2C initialized\n");

    // Check manufacturer ID
    uint16_t manuf_id, die_id;
    if (!ina226_read_register(INA226_REG_MANUF_ID, &manuf_id) ||
        !ina226_read_register(INA226_REG_DIE_ID, &die_id))
    {
        printf("INA226: Failed to read device IDs\n");
        return false;
    }

    printf("INA226: Manufacturer ID: 0x%04X\n", manuf_id);
    printf("INA226: Die ID: 0x%04X\n", die_id);

    if (manuf_id == 0x5449 && (die_id & 0xFFF0) == 0x2260)
    {
        printf("INA226: Device identification successful!\n");
        blink_led(3); // Success blinks
    }
    else
    {
        printf("INA226: Warning - Unexpected device IDs\n");
        blink_led(2); // Warning blinks
    }

    // Reset device
    if (!ina226_write_register(INA226_REG_CONFIG, INA226_CONFIG_RESET))
    {
        printf("INA226: Failed to reset device\n");
        return false;
    }

    sleep_ms(100);

    // Configure device
    if (!ina226_configure())
    {
        printf("INA226: Failed to configure device\n");
        return false;
    }

    // Calibrate device
    if (!ina226_calibrate())
    {
        printf("INA226: Failed to calibrate device\n");
        return false;
    }

    printf("INA226: Initialization complete!\n");
    return true;
}

bool ina226_configure(void)
{
    // Configuration: 16 averages, 1.1ms conversion times, continuous mode
    uint16_t config = INA226_CONFIG_AVG_16 |
                      INA226_CONFIG_VBUSCT_1100US |
                      INA226_CONFIG_VSHCT_1100US |
                      INA226_CONFIG_MODE_SHUNT_BUS_CONT;

    if (!ina226_write_register(INA226_REG_CONFIG, config))
    {
        return false;
    }

    printf("INA226: Configured for continuous measurement\n");
    return true;
}

bool ina226_calibrate(void)
{
    // Calculate Current_LSB
    float current_lsb = MAX_EXPECTED_CURRENT / 32767.0;
    current_lsb = 0.001; // Use 1mA per LSB for simplicity

    // Calculate calibration register value
    float cal_value = 0.00512 / (current_lsb * SHUNT_RESISTOR_OHMS);
    uint16_t calibration_reg = (uint16_t)(cal_value + 0.5);

    if (!ina226_write_register(INA226_REG_CALIBRATION, calibration_reg))
    {
        return false;
    }

    printf("INA226: Calibrated - Shunt: %.1f mΩ, Current LSB: %.3f mA\n",
           SHUNT_RESISTOR_OHMS * 1000, current_lsb * 1000);
    printf("INA226: Calibration register: 0x%04X (%d)\n", calibration_reg, calibration_reg);

    return true;
}

bool ina226_read_all_data(void)
{
    // Read all measurement registers
    if (!ina226_read_register(INA226_REG_SHUNT_V, &sensor_data.raw_shunt) ||
        !ina226_read_register(INA226_REG_BUS_V, &sensor_data.raw_bus) ||
        !ina226_read_register(INA226_REG_CURRENT, &sensor_data.raw_current) ||
        !ina226_read_register(INA226_REG_POWER, &sensor_data.raw_power))
    {
        return false;
    }

    // Convert raw values to engineering units

    // Shunt voltage (signed 16-bit value)
    int16_t signed_shunt = (int16_t)sensor_data.raw_shunt;
    sensor_data.shunt_voltage_mv = signed_shunt * INA226_SHUNT_LSB_UV / 1000.0;

    // Bus voltage (15-bit value, MSB is always 0)
    sensor_data.bus_voltage_v = (sensor_data.raw_bus & 0x7FFF) * INA226_BUS_LSB_MV / 1000.0;

    // Current (signed 16-bit value, 1mA per LSB)
    int16_t signed_current = (int16_t)sensor_data.raw_current;
    sensor_data.current_a = signed_current * 0.001; // 1mA per LSB

    // Power (unsigned 16-bit value, 25mW per LSB)
    sensor_data.power_w = sensor_data.raw_power * 0.025; // 25mW per LSB

    return true;
}

void ina226_print_data(void)
{
    printf("Raw: S:0x%04X B:0x%04X C:0x%04X P:0x%04X | ",
           sensor_data.raw_shunt, sensor_data.raw_bus,
           sensor_data.raw_current, sensor_data.raw_power);
    printf("Shunt:%+7.3fmV Bus:%6.3fV Current:%+7.3fA Power:%7.3fW\n",
           sensor_data.shunt_voltage_mv, sensor_data.bus_voltage_v,
           sensor_data.current_a, sensor_data.power_w);
}

void configure_dma()
{
    data_chan = dma_claim_unused_channel(true);
    dma_channel_config c = dma_channel_get_default_config(data_chan);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_8); // 8-bit txfers
    channel_config_set_read_increment(&c, true);           // yes read incrementing
    channel_config_set_write_increment(&c, false);         // no write incrementing
    channel_config_set_dreq(&c, DREQ_SPI0_TX);

    dma_channel_configure(
        data_chan,                 // Channel to be configured
        &c,                        // The configuration we just created
        &spi_get_hw(SPI_PORT)->dr, // write address (SPI data register)
        tx_data,                   // The initial read address
        LEN,                       // Number of transfers
        false                      // Don't start immediately.
    );
}

void irq_handler(uint gpio, uint32_t events)
{
    if (events & GPIO_IRQ_EDGE_FALL)
    {
        gpio_set_function(PIN_TX, GPIO_FUNC_SPI);
        ina226_read_all_data()
            pack_all_sensor_data();
        tx_data[10] = 0x00; // End byte
        dma_hw->ch[data_chan].read_addr = (uintptr_t)tx_data;
        dma_start_channel_mask(1u << data_chan);
        printf("\nSent: ");
        for (int i = 0; i < LEN; i++)
        {
            printf("%02X ", tx_data[i]);
        }
    }
    else if (events & GPIO_IRQ_EDGE_RISE)
    {
        // printf("here\n");
        set_gpio_hi_z(PIN_TX);
    }
}
int initialize()
{
    stdio_init_all();
    // Initialize LED
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);
    // Trigger interrupt on falling/rising edges
    gpio_init(TRIGGER);
    gpio_set_dir(TRIGGER, GPIO_IN);
    gpio_set_irq_enabled_with_callback(TRIGGER, GPIO_IRQ_EDGE_FALL | GPIO_IRQ_EDGE_RISE, true, &irq_handler);
    sleep_ms(2000);
    printf("\n\n========================================\n");
    printf("INA226 Current/Power Monitor - Data Mode\n");
    printf("========================================\n\n");
    // Initialize INA226
    if (!ina226_init())
    {
        printf("FAILED: Could not initialize INA226!\n");
        printf("Check your connections and try again.\n");
        // Error blink pattern
        while (1)
        {
            blink_led(5);
            sleep_ms(2000);
        }
    }
    printf("\nNote: Connect your load between IN+ and IN- pins\n");
    printf("      Connect VBUS to the voltage you want to monitor\n");
    printf("      Current shunt resistor configured for: %.1f mΩ\n\n",
           SHUNT_RESISTOR_OHMS * 1000);
    printf("Starting measurements (every 1 second):\n");
    printf("Raw values shown for debugging, then converted values\n\n");
}
int main()
{
    initialize();
    configure_dma();
    return 0;
}