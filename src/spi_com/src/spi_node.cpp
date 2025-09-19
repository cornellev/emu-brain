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
#include "spi_com/msg/strain_gauge.hpp"

using namespace std::chrono_literals;

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

        mech_pub_ = this->create_publisher<spi_com::msg::StrainGauge>("strain_gauge", 1);
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
    rclcpp::Publisher<spi_com::msg::StrainGauge>::SharedPtr mech_pub_;
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
        transfer.len = 11;  
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
        std::vector<uint8_t> p1 = readData(1); 
        std::vector<uint8_t> p2 = readData(2);
        std::vector<uint8_t> p3 = readData(3);

        uint32_t timestamp = 0;
        uint16_t adc1 = 0;
        uint16_t adc2 = 0;
        uint16_t adc3 = 0;
        uint16_t adc4 = 0;
        uint16_t adc5 = 0;
        uint16_t adc6 = 0;

        timestamp = (p1[4]) | (p1[3] << 8) | (p1[2] << 16) | (p1[1] << 24);
        adc1 = (p1[6]) | (p1[5] << 8);
        adc2 = (p1[8]) | (p1[7] << 8);
        adc3 = (p1[10]) | (p1[9] << 8);
        auto m1 = spi_com::msg::StrainGauge();
        m1.timestamp = timestamp;
        m1.sensor1 = adc1;
        m1.sensor2 = adc2;
        m1.sensor3 = adc3;
        mech_pub_->publish(m1);

        // RCLCPP_INFO(this->get_logger(), "Received SPI data:");
        // for (int i = 0; i < 11; i++){
        //     RCLCPP_INFO(this->get_logger(), "0x%02X", p1[i]);
        // }
        // RCLCPP_INFO(this->get_logger(), "Timestamp: %u", timestamp);
        // RCLCPP_INFO(this->get_logger(), "ADC1: %u", adc1);  
        // RCLCPP_INFO(this->get_logger(), "ADC2: %u", adc2);  
        // RCLCPP_INFO(this->get_logger(), "ADC3: %u", adc3);  

        rclcpp::sleep_for(20ms);

        timestamp = (p2[4]) | (p2[3] << 8) | (p2[2] << 16) | (p2[1] << 24);
        adc4 = (p2[6]) | (p2[5] << 8);
        adc5 = (p2[8]) | (p2[7] << 8);
        adc6 = (p2[10]) | (p2[9] << 8);
        auto m2 = spi_com::msg::StrainGauge();
        m2.timestamp = timestamp;
        m2.sensor4 = adc4;
        m2.sensor5 = adc5;
        m2.sensor6 = adc6;
        mech_pub_->publish(m2);

        RCLCPP_INFO(this->get_logger(), "Received SPI data:");
        // for (int i = 0; i < 11; i++){
        //     RCLCPP_INFO(this->get_logger(), "0x%02X", p2[i]);
        // }
        RCLCPP_INFO(this->get_logger(), "Timestamp: %u", timestamp);
        RCLCPP_INFO(this->get_logger(), "ADC4: %u", adc4);  
        RCLCPP_INFO(this->get_logger(), "ADC5: %u", adc5);  
        RCLCPP_INFO(this->get_logger(), "ADC6: %u", adc6);  
    }
};

int main(int argc, char ** argv) {
    rclcpp::init(argc, argv);
    rclcpp::spin(std::make_shared<SPINode>());
    rclcpp::shutdown();
    return 0;
}
