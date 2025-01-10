cmake_minimum_required(VERSION 3.10)

# Set the project name
project(X2JPEG)

# Set the C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Define the output binary name
set(TARGET_NAME X2JPEG)

# Add the executable
add_executable(${TARGET_NAME} main.cpp)

# Add required packages
find_package(OpenCV REQUIRED)
find_package(X11 REQUIRED)
find_package(Threads REQUIRED)

# Link libraries
target_link_libraries(${TARGET_NAME}
    ${OpenCV_LIBS}
    ${X11_LIBRARIES}
    Threads::Threads
    websocketpp
    ${CMAKE_DL_LIBS}
)

# Include directories
target_include_directories(${TARGET_NAME} PRIVATE
    ${OpenCV_INCLUDE_DIRS}
    ${X11_INCLUDE_DIR}
)
