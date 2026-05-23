cmake_minimum_required(VERSION 3.16)
project(dddcli VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(LibUSB REQUIRED IMPORTED_TARGET libusb-1.0)
include(CTest)

add_executable(dddcli
    src/main.cpp
    src/CliConfig.cpp
    src/ConsoleLogger.cpp
    src/CaptureMetadata.cpp
    src/PlayerSerial.cpp
    src/ProgressLine.cpp
    src/UsbDeviceBase.cpp
    src/UsbDeviceLibUsb.cpp
)

target_include_directories(dddcli PRIVATE
    src
)

target_link_libraries(dddcli PRIVATE
    PkgConfig::LibUSB
    pthread
)

install(TARGETS dddcli)

if(BUILD_TESTING)
    add_executable(dddcli_tests
        tests/CliConfigTests.cpp
        src/CliConfig.cpp
        src/PlayerSerial.cpp
        src/ProgressLine.cpp
    )
    target_include_directories(dddcli_tests PRIVATE
        src
    )
    add_test(NAME dddcli_tests COMMAND dddcli_tests)
endif()
