#include "rclcpp/rclcpp.hpp"
#include "sensor_msgs/msg/joy.hpp"
#include "std_msgs/msg/string.hpp"

using namespace std::chrono_literals;

class JoyInterpreter : public rclcpp::Node {
    public:
        JoyInterpreter() : Node("joy_interpreter") {
            subscriber_ = this->create_subscription<sensor_msgs::msg::Joy>("joy", 1, std::bind(&JoyInterpreter::joy_callback, this, std::placeholders::_1));
        }

    private:
        void joy_callback(const sensor_msgs::msg::Joy & msg) const {
            RCLCPP_INFO(this->get_logger(), "I heard: %f", msg.axes[5]);
        }

        rclcpp::Subscription<sensor_msgs::msg::Joy>::SharedPtr subscriber_;
};

int main(int argc, char * argv[]) {
    rclcpp::init(argc, argv);
    rclcpp::spin(std::make_shared<JoyInterpreter>());
    rclcpp::shutdown();
    return 0;
}