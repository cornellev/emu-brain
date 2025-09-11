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
#include "i2c_com/msg/strain_gauge.hpp"

#define PICO_ADDR 0x42  
#define REG_DATA 0x00 

class I2CNode : public rclcpp::Node {
public:
    I2CNode() : Node("i2c_node") {
        publisher_ = this->create_publisher<i2c_com::msg::StrainGauge>("i2c_data", 1);

        i2c_fd_ = open("/dev/i2c-1", O_RDWR);
        if (i2c_fd_ < 0) {
            RCLCPP_ERROR(this->get_logger(), "Failed to open I2C bus");
            return;
        }

        if (ioctl(i2c_fd_, I2C_SLAVE, PICO_ADDR) < 0) {
            RCLCPP_ERROR(this->get_logger(), "Failed to select I2C device");
            return;
        }

        timer_ = this->create_wall_timer(std::chrono::milliseconds(100), std::bind(&I2CNode::timer_callback, this));
    }

    ~I2CNode() {
        if (i2c_fd_ >= 0) {
            close(i2c_fd_);
        }
    }

private:
    int i2c_fd_;
    rclcpp::Publisher<i2c_com::msg::StrainGauge>::SharedPtr publisher_;
    rclcpp::TimerBase::SharedPtr timer_;

    int32_t read_pico() {
        uint8_t reg = REG_DATA; 
        uint8_t buffer[4];

        // send desired register address to pico
        if (write(i2c_fd_, &reg, 1) != 1) {
            RCLCPP_ERROR(this->get_logger(), "Failed to write register address");
            return 0; 
        }

        // read data (2 bytes) from pico
        if (read(i2c_fd_, buffer, 4) != 4) {
            RCLCPP_ERROR(this->get_logger(), "Failed to read data from Pico");
            return 0;
        }

        // combine 2 bytes into a 16-bit value
        return (int32_t)((buffer[0] << 24) | (buffer[1] << 16) | (buffer[2] << 8) | buffer[3]);
    }

    void timer_callback() {
        int32_t raw_value = read_pico();

        // Unpack the 32-bit value into two 16-bit values
        int16_t sensor1 = (int16_t)((raw_value >> 16) & 0xFFFF);  // High 16 bits
        int16_t sensor2 = (int16_t)(raw_value & 0xFFFF);          // Low 16 bits

        // Convert raw sensor data to voltage (assuming 16-bit ADC with a 4.096V reference)
        float sensor1_voltage = (sensor1 * 4.096f) / 32768.0f;
        float sensor2_voltage = (sensor2 * 4.096f) / 32768.0f;

        // Create the message and assign voltage values for both sensors
        auto test = i2c_com::msg::StrainGauge();
        test.sensor1 = sensor1_voltage;
        test.sensor2 = sensor2_voltage;
        publisher_->publish(test);

        // Log the result
        // RCLCPP_INFO(this->get_logger(), "%f", voltage);
        RCLCPP_INFO(this->get_logger(), "Sensor1 Voltage: %f, Sensor2 Voltage: %f", sensor1_voltage, sensor2_voltage);
    }
};

int main(int argc, char ** argv)
{
    rclcpp::init(argc, argv);
    rclcpp::spin(std::make_shared<I2CNode>());
    rclcpp::shutdown();
    return 0;
}
