cmake_minimum_required(VERSION 3.8)
project(i2c_com)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES Clang)
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(rclcpp REQUIRED)
find_package(std_msgs REQUIRED)
find_package(ament_cmake REQUIRED)

# This includes the necessary I2C headers
include_directories(SYSTEM /usr/include)

add_executable(i2c_com src/i2c_com.cpp)
ament_target_dependencies(i2c_com rclcpp std_msgs)

# Install the node executable and other resources
install(TARGETS
  i2c_com
  DESTINATION lib/${PROJECT_NAME}
)

# Add dependencies for the ROS2 package
ament_package()
