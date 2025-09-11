#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/string.hpp"
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <sstream>
#include <string>
#include <chrono>
#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/string.hpp>
#include <unistd.h>  // For sleep

// I2C Defines for ADS1115
#define I2C_ADDR_1 0x48
#define I2C_ADDR_2 0x49
#define AIN0 0xC3
#define AIN1 0xD3
#define AIN2 0xE3
#define AIN3 0xF3
#define REG_CONVERSION 0x00
#define REG_CONFIG     0x01

class I2CCom : public rclcpp::Node {
public:
    I2CCom() : Node("i2c_com") {
        // Initialize the publisher
        publisher_ = this->create_publisher<std_msgs::msg::String>("i2c_data", 10);

        // Open the I2C bus (usually "/dev/i2c-1")
        i2c_fd_ = open("/dev/i2c-1", O_RDWR);
        if (i2c_fd_ < 0) {
            RCLCPP_ERROR(this->get_logger(), "Failed to open I2C bus");
            return;
        }

        if (ioctl(i2c_fd_, I2C_SLAVE, I2C_ADDR_1) < 0) {
            RCLCPP_ERROR(this->get_logger(), "Failed to select I2C device");
            return;
        }

        // Timer to read data from the I2C bus every 1 second
        timer_ = this->create_wall_timer(std::chrono::milliseconds(100), std::bind(&I2CCom::read_i2c_data, this));
    }

    ~I2CCom()
    {
        if (i2c_fd_ >= 0) {
            close(i2c_fd_);
        }
    }

private:
    int i2c_fd_;
    rclcpp::Publisher<std_msgs::msg::String>::SharedPtr publisher_;
    rclcpp::TimerBase::SharedPtr timer_;

    void ads1115_start_conversion(uint8_t addr, uint8_t input) {
        uint8_t config_data[3];

        // ADS1115 config register: 0xC3E3 (single-shot, AIN0, ±4.096V, 860SPS)
        config_data[0] = REG_CONFIG;
        config_data[1] = input; // High byte of config
        config_data[2] = 0xE3; // Low byte of config

        while (ioctl(i2c_fd_, I2C_SLAVE, addr) < 0) {
            // RCLCPP_ERROR(this->get_logger(), "Failed to select I2C device at address: 0x%x", addr);
            // return;
        }

        while (write(i2c_fd_, config_data, 3) != 3) {
            // RCLCPP_ERROR(this->get_logger(), "Failed to write config to ADS1115");
        }
    }

    int16_t ads1115_read_conversion(uint8_t addr) {
        uint8_t reg = REG_CONVERSION;
        uint8_t buffer[2];

        while (ioctl(i2c_fd_, I2C_SLAVE, addr) < 0) {
            // RCLCPP_ERROR(this->get_logger(), "Failed to select I2C device at address: 0x%x", addr);
            // return 0;
        }

        // Send the address of the conversion register
        while (write(i2c_fd_, &reg, 1) != 1) {
            // RCLCPP_ERROR(this->get_logger(), "Failed to write conversion register address");
            // return 0;
        }

        // Read the conversion data (2 bytes)
        while (read(i2c_fd_, buffer, 2) != 2) {
            // RCLCPP_ERROR(this->get_logger(), "Failed to read conversion data");
            // return 0;
        }

        // Combine the 2 bytes into a 16-bit value
        return (int16_t)((buffer[0] << 8) | buffer[1]);
    }

    void read_i2c_data()
    {
        ads1115_start_conversion(I2C_ADDR_1, AIN1);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        int16_t raw1 = ads1115_read_conversion(I2C_ADDR_1);
        float voltage1 = (raw1 * 4.096f) / 32768.0f;

        ads1115_start_conversion(I2C_ADDR_2, AIN0);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        int16_t raw2 = ads1115_read_conversion(I2C_ADDR_2);
        float voltage2 = (raw2 * 4.096f) / 32768.0f;

        // auto msg = std_msgs::msg::String();
        // msg.data = "ADC1 Voltage: " + std::to_string(voltage1);
        // publisher_->publish(msg);

        RCLCPP_INFO(this->get_logger(), "Voltage: %f \t %f", voltage1, voltage2);
    }
};

int main(int argc, char ** argv)
{
    rclcpp::init(argc, argv);
    rclcpp::spin(std::make_shared<I2CCom>());
    rclcpp::shutdown();
    return 0;
}
