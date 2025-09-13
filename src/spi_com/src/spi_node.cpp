#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/string.hpp"
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sstream>
#include <string>
#include <chrono>
#include <linux/spi/spidev.h>
#include <pigpio.h>

#define SPI_DEV "/dev/spidev0.0"  // The SPI device file (for CS0)
#define CS_PIN 8 // Chip Select Pin (GPIO8 - CE0)

class SPINode : public rclcpp::Node {
public:
    SPINode() : Node("spi_node") {
        
        // Open SPI device
        spi_fd_ = open(SPI_DEV, O_RDWR);
        if (spi_fd_ < 0) {
            RCLCPP_ERROR(this->get_logger(), "Failed to open SPI device");
            return;
        }

        // Set SPI mode (CPOL=0, CPHA=0)
        if (ioctl(spi_fd_, SPI_IOC_WR_MODE, &mode) < 0) {
            RCLCPP_ERROR(this->get_logger(), "Failed to set SPI mode");
            close(spi_fd_);
            return;
        }

        // Set number of bits per word (8 bits)
        if (ioctl(spi_fd_, SPI_IOC_WR_BITS_PER_WORD, &bits) < 0) {
            RCLCPP_ERROR(this->get_logger(), "Failed to set SPI bits per word");
            close(spi_fd_);
            return;
        }

        // Set maximum speed for SPI (500 kHz)
        if (ioctl(spi_fd_, SPI_IOC_WR_MAX_SPEED_HZ, &speed) < 0) {
            RCLCPP_ERROR(this->get_logger(), "Failed to set SPI max speed");
            close(spi_fd_);
            return;
        }

        RCLCPP_INFO(this->get_logger(), "SPI device initialized");

        // Create a timer to periodically read from the SPI device
        timer_ = this->create_wall_timer(std::chrono::milliseconds(100), std::bind(&SPINode::timer_callback, this));

        // Initialize ROS 2 publisher to send received SPI data
        publisher_ = this->create_publisher<std_msgs::msg::String>("spi_data", 10);
    }

    ~SPINode() {
        if (spi_fd_ >= 0) {
            close(spi_fd_);
        }
    }

private:
    int spi_fd_;
    uint8_t mode = SPI_MODE_0;    // SPI mode (CPOL = 0, CPHA = 0)
    uint8_t bits = 8;             // 8 bits per transfer
    uint32_t speed = 500000;      // SPI speed (500kHz)
    rclcpp::TimerBase::SharedPtr timer_;
    rclcpp::Publisher<std_msgs::msg::String>::SharedPtr publisher_;

    uint8_t read_pico() {
        uint8_t tx_buffer[1] = {0x00};  
        uint8_t rx_buffer[1];           

        struct spi_ioc_transfer transfer;
        transfer.tx_buf = (unsigned long)tx_buffer;
        transfer.rx_buf = (unsigned long)rx_buffer;
        transfer.len = sizeof(tx_buffer);
        transfer.speed_hz = speed;
        transfer.bits_per_word = bits;

        if (ioctl(spi_fd_, SPI_IOC_MESSAGE(1), &transfer) < 0) {
            RCLCPP_ERROR(this->get_logger(), "SPI transfer failed!");
            return 0x00;
        }
        
        return rx_buffer[0];
    }

    // Timer callback to periodically read from the SPI device
    void timer_callback() {
        uint8_t raw_value = read_pico();
        RCLCPP_INFO(this->get_logger(), "Raw Value: %d", raw_value);

        // auto message = std_msgs::msg::String();
        // message.data = std::to_string(raw_value);
        // publisher_->publish(message);
    }
};

int main(int argc, char ** argv) {
    rclcpp::init(argc, argv);
    rclcpp::spin(std::make_shared<SPINode>());
    rclcpp::shutdown();
    return 0;
}
