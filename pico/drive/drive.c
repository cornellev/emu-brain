/*
 * This file contains the implementation of the motor control system.
 * It handles the PWM signals, ADC readings, and UART communication.
 */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/pwm.h"
#include "hardware/irq.h"
#include "hardware/adc.h"
#include "hardware/uart.h"
#include "hardware/spi.h"
#include "hardware/dma.h"
#include "pico/time.h"
#define LED 25

#define SPI_PORT spi0
#define PIN_RX 16
#define PIN_TX 19
#define PIN_SCK  18
#define PIN_CS   17

#define LEN 12
#define TRIGGER 17

uint8_t tx_data[LEN]; 
int data_chan ;

// Hall sensor inputs
#define NUM_INPUTS 3
const uint input_pins[NUM_INPUTS] = {13, 14, 15};

const uint pwm_pins[2] = {6, 10};
uint pwm_slices[2] ;
const uint enable_pins[2] = {7, 11};
const uint brake_pins[2] = {8, 12};
const uint dir_pins[2] = {9, 13};

#define HALL_TEST 5
#define THROTTLE_ADC 26

#define TAU 1e6 // time constant in us for low-pass filter

volatile float motor_rpm = 0.0f;
const float RATED_MOTOR_RPM = 3000.0f;
const float RATED_MOTOR_VOLTAGE = 48.0f;
const float MAX_VOLTAGE_AT_STALL = 10.0f;
volatile float throttle = 0.0f;

const int PWM_FREQ = 5000;
const int WRAPVAL = SYS_CLK_HZ / PWM_FREQ - 1;
const float MIN_RPM = 25.0f;

// if the motor is stationary, then the interrupt needs to be called
// periodically to avoid the interrupt from never being called
// and the motor_rpm from never being updated
struct repeating_timer timer;

// time of last irq
volatile uint32_t irq_prev_time = 0;

void set_gpio_hi_z(uint pin) {
    io_bank0_hw->io[pin].ctrl = (io_bank0_hw->io[pin].ctrl & ~IO_BANK0_GPIO0_CTRL_OEOVER_BITS) | (IO_BANK0_GPIO0_CTRL_OEOVER_VALUE_DISABLE << IO_BANK0_GPIO0_CTRL_OEOVER_LSB);
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

bool timer_callback(struct repeating_timer *t)
{
    // no velocity on boot
    if (irq_prev_time == 0) {
        motor_rpm = 0.0f;
    }

    int timer_current_time = time_us_64();
    float timer_period = (float)(timer_current_time - irq_prev_time);
    
    // low-pass filter
    float raw_rpm = 2.5e6f / timer_period; 
    float alpha = timer_period / (TAU + timer_period);
    motor_rpm = alpha * raw_rpm + (1.0f - alpha) * motor_rpm;

    // minimum measurable rpm is 25 rpm
    if (motor_rpm < MIN_RPM)
        motor_rpm = 0.0f;
    
    cancel_repeating_timer(&timer);
    add_repeating_timer_ms(100, (repeating_timer_callback_t)timer_callback, NULL, &timer);
    return true;
}

void irq_handler(uint gpio, uint32_t events)
{
    if ((gpio == TRIGGER) && (events & GPIO_IRQ_EDGE_FALL)) {
        gpio_set_function(PIN_TX, GPIO_FUNC_SPI);

        uint32_t now_us = (uint32_t)time_us_64();
        tx_data[0] = (now_us >> 24) & 0xFF;
        tx_data[1] = (now_us >> 16) & 0xFF;
        tx_data[2] = (now_us >> 8) & 0xFF;
        tx_data[3] = (now_us >> 0) & 0xFF;
        pack_data_float(4, throttle);
        pack_data_float(8, motor_rpm);

        dma_hw->ch[data_chan].read_addr = (uintptr_t)tx_data;
        dma_start_channel_mask(1u << data_chan);

        // printf("\nSent: ");
        // for (int i = 0; i < LEN; i++) {
        //     printf("%02X ", tx_data[i]);
        // }
    } else if ((gpio == TRIGGER) && (events & GPIO_IRQ_EDGE_RISE)) {
        set_gpio_hi_z(PIN_TX);
    } else {
        int irq_current_time = time_us_64();

        // time between steps in microseconds
        float step_period = (float)(irq_current_time - irq_prev_time);
        if (step_period <= 800.0f) { // invalid period, ignore
            return;
        }

        irq_prev_time = irq_current_time;

        // step/us * 1 elec. rev/6 steps * 1 mech. rev/4 elec. rev * 1e6 us/s * 60 s/min
        // = 2.5e6 rpm
        float raw_rpm = 2.5e6f / step_period; 

        // low-pass filter
        float alpha = step_period / (TAU + step_period);
        motor_rpm = alpha * raw_rpm + (1.0f - alpha) * motor_rpm;

        // reset the timer to call this function again in 100ms if no step is detected
        cancel_repeating_timer(&timer);
        add_repeating_timer_ms(100, (repeating_timer_callback_t)timer_callback, NULL, &timer);
    }
}

// Convert a duty cycle percentage (0 to 1) to a level value (0 to PWM_WRAP)
int duty_cycle_to_level(float duty_cycle)
{
    if (duty_cycle < 0.0f)
        duty_cycle = 0.0f;
    if (duty_cycle > 1.0f)
        duty_cycle = 1.0f;
    return (int)(duty_cycle * WRAPVAL + 1);
}

int adc_deadzone(int adc_value)
{
    const int DEADZONE = 100; // adc units

    if (adc_value < DEADZONE)
        return 0;
    if (adc_value > 4095 - DEADZONE)
        return 4095;
    adc_value = (adc_value - DEADZONE) * 4095 / (4095 - DEADZONE);
    return adc_value;
}

void initialize()
{
    // Turn on onboard LED
    gpio_init(LED);
    gpio_set_dir(LED, GPIO_OUT);
    gpio_put(LED, 1);

    // Test PWM output to imitate hall sensors:
    gpio_set_function(HALL_TEST, GPIO_FUNC_PWM);
    uint slice_num = pwm_gpio_to_slice_num(HALL_TEST);
    float test_rpm = 1500.0f;
    int step_period_clks = (int)((312.5e6f / test_rpm / 256.0 * 2) - 1);
    // set clock divider to 256
    pwm_set_clkdiv(slice_num, 256.0f);
    pwm_set_wrap(slice_num, step_period_clks);
    pwm_set_chan_level(slice_num, PWM_CHAN_B, step_period_clks / 2);
    pwm_set_enabled(slice_num, true);

    gpio_init(TRIGGER);
    gpio_set_dir(TRIGGER, GPIO_IN);
    gpio_set_irq_enabled_with_callback(TRIGGER, GPIO_IRQ_EDGE_FALL | GPIO_IRQ_EDGE_RISE, true, &irq_handler);
    
    spi_init(SPI_PORT, 1000 * 1000);  
    spi_set_slave(SPI_PORT, true);   
    gpio_set_function(PIN_RX, GPIO_FUNC_SPI);
    gpio_set_function(PIN_TX, GPIO_FUNC_SPI);
    gpio_set_function(PIN_SCK, GPIO_FUNC_SPI);
    // gpio_set_function(PIN_CS, GPIO_FUNC_SPI);  
    spi_set_format(spi0, 8, SPI_CPOL_0, SPI_CPHA_1, false);

    for (int i = 0; i < 2; i++) {
        gpio_set_function(pwm_pins[i], GPIO_FUNC_PWM);
        pwm_slices[i] = pwm_gpio_to_slice_num(pwm_pins[i]);
        pwm_set_wrap(pwm_slices[i], WRAPVAL);
        pwm_set_chan_level(pwm_slices[i], PWM_CHAN_A, 0);
        pwm_set_enabled(pwm_slices[i], true);

        gpio_init(enable_pins[i]);
        gpio_set_dir(enable_pins[i], GPIO_OUT);
        gpio_put(enable_pins[i], 1);

        gpio_init(brake_pins[i]);
        gpio_set_dir(brake_pins[i], GPIO_OUT);
        gpio_put(brake_pins[i], 1);

        gpio_init(dir_pins[i]);
        gpio_set_dir(dir_pins[i], GPIO_OUT);
    }

    gpio_put(dir_pins[0], 0);
    gpio_put(dir_pins[1], 1);

    for (int i = 0; i < NUM_INPUTS; i++)
    {
        gpio_init(input_pins[i]);
        gpio_set_dir(input_pins[i], GPIO_IN);
        gpio_set_irq_enabled(input_pins[i], GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true);
    }

    // add a timer that will call the irq_handler if no step has been detected for 100ms
    add_repeating_timer_ms(100, (repeating_timer_callback_t)timer_callback, NULL, &timer);

    // configure adc for throttle input
    gpio_init(THROTTLE_ADC);
    gpio_set_dir(THROTTLE_ADC, GPIO_IN);
    adc_init();
    adc_gpio_init(THROTTLE_ADC);
    adc_select_input(0);
}

int main()
{
    stdio_init_all();
    initialize();
    configure_dma();

    while (true)
    {
        throttle = (float)adc_deadzone(adc_read()) / 4095.0f;

        // Calculate duty cycle
        float duty = throttle * (motor_rpm / RATED_MOTOR_RPM + MAX_VOLTAGE_AT_STALL / RATED_MOTOR_VOLTAGE);
        uint64_t ms = time_us_64() / 1000;
        // printf("%u,%.5f,%.5f\n", ms, throttle, motor_rpm);

        // Write float to PWM output
        pwm_set_chan_level(pwm_slices[0], PWM_CHAN_A, duty_cycle_to_level(duty));
        pwm_set_chan_level(pwm_slices[1], PWM_CHAN_A, duty_cycle_to_level(duty));
    }
}
