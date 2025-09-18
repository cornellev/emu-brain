#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/string.hpp"
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sstream>
#include <string>
#include <chrono>
#include <linux/spi/spidev.h>
#include <pigpiod_if2.h>

constexpr uint8_t SPI_MODE = 1;          // CPOL=0, CPHA=1
constexpr uint32_t SPI_SPEED = 1000000;  // SPI speed (1 MHz)
constexpr const char* SPI_DEVICE = "/dev/spidev0.0";

#define GPIO_CS1 25         // CS1
#define GPIO_CS2 27         // CS2
#define GPIO_CS3 22         // CS3

class SPINode : public rclcpp::Node {
public:
    SPINode() : Node("spi_node") {

        pi_ = pigpio_start(NULL, NULL);
        if (pi_ < 0) {
            RCLCPP_ERROR(this->get_logger(), "Failed to connect to pigpiod");
            rclcpp::shutdown();
            return;
        } else {
            RCLCPP_INFO(this->get_logger(), "pigpio initialized");
        } 

        set_mode(pi_, GPIO_CS1, PI_OUTPUT);
        set_mode(pi_, GPIO_CS2, PI_OUTPUT);
        set_mode(pi_, GPIO_CS3, PI_OUTPUT);

        gpio_write(pi_, GPIO_CS1, 1);
        gpio_write(pi_, GPIO_CS2, 1);
        gpio_write(pi_, GPIO_CS3, 1);
        
        // Open SPI device
        spi_fd_ = open(SPI_DEVICE, O_RDWR);
        if (spi_fd_ < 0) {
            RCLCPP_ERROR(this->get_logger(), "Failed to open SPI device");
            return;
        }

        // Set SPI mode
        if (ioctl(spi_fd_, SPI_IOC_WR_MODE, &SPI_MODE) < 0) {
            RCLCPP_ERROR(this->get_logger(), "Failed to set SPI mode");
            close(spi_fd_);
            return;
        }

        // Set speed
        if (ioctl(spi_fd_, SPI_IOC_WR_MAX_SPEED_HZ, &SPI_SPEED) < 0) {
            RCLCPP_ERROR(this->get_logger(), "Failed to set SPI max speed");
            close(spi_fd_);
            return;
        }

        RCLCPP_INFO(this->get_logger(), "SPI device initialized");

        timer_ = this->create_wall_timer(std::chrono::milliseconds(100), std::bind(&SPINode::timer_callback, this));
    }

    ~SPINode() {
        if (spi_fd_ >= 0) {
            close(spi_fd_);
        }
        if (pi_ >= 0) {
            pigpio_stop(pi_);
        }
    }

private:
    int spi_fd_;
    int pi_{-1};
    rclcpp::TimerBase::SharedPtr timer_;

    std::vector<uint8_t> readData(int chipSelect)
    {

        gpio_write(pi_, GPIO_CS1, chipSelect == 1 ? 0 : 1);
        gpio_write(pi_, GPIO_CS2, chipSelect == 2 ? 0 : 1);
        gpio_write(pi_, GPIO_CS3, chipSelect == 3 ? 0 : 1);

        std::vector<uint8_t> tx(10, 0x00);  
        std::vector<uint8_t> rx(10, 0x00);  

        // SPI transfer structure   
        struct spi_ioc_transfer transfer = {};
        transfer.tx_buf = reinterpret_cast<unsigned long>(tx.data());
        transfer.rx_buf = reinterpret_cast<unsigned long>(rx.data());
        transfer.len = 10;  
        transfer.speed_hz = SPI_SPEED;
        transfer.bits_per_word = 8;

        if (ioctl(spi_fd_, SPI_IOC_MESSAGE(1), &transfer) < 0) {
            RCLCPP_ERROR(this->get_logger(), "SPI transfer failed.");
        }
        
        gpio_write(pi_, GPIO_CS1, 1);
        gpio_write(pi_, GPIO_CS2, 1);
        gpio_write(pi_, GPIO_CS3, 1);

        return rx;

        // RCLCPP_INFO(this->get_logger(), "Received SPI data: ");
        // for (int i = 0; i < 10; ++i)
        // {
        //     RCLCPP_INFO(this->get_logger(), "0x%02X", rx[i]);
        // }
    }

    // Timer callback to periodically read from the SPI device
    void timer_callback() {
        std::vector<uint8_t> rx = readData(1); 

        uint32_t timestamp = 0;
        uint16_t adc1 = 0;
        uint16_t adc2 = 0;
        uint16_t adc3 = 0;

        timestamp = (rx[3]) | (rx[2] << 8) | (rx[1] << 16) | (rx[0] << 24);
        adc1 = (rx[5]) | (rx[4] << 8);
        adc2 = (rx[7]) | (rx[6] << 8);
        adc3 = (rx[9]) | (rx[8] << 8);

        RCLCPP_INFO(this->get_logger(), "Received SPI data:");
        RCLCPP_INFO(this->get_logger(), "Timestamp: %u", timestamp);
        RCLCPP_INFO(this->get_logger(), "ADC1: %u", adc1);  
        RCLCPP_INFO(this->get_logger(), "ADC2: %u", adc2);  
        RCLCPP_INFO(this->get_logger(), "ADC3: %u", adc3);  
    }
};

int main(int argc, char ** argv) {
    rclcpp::init(argc, argv);
    rclcpp::spin(std::make_shared<SPINode>());
    rclcpp::shutdown();
    return 0;
}
