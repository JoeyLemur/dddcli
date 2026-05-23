cmake_minimum_required(VERSION 3.16)
project(dddcli VERSION 1.0 LANGUAGES CXX)

list(APPEND CMAKE_MODULE_PATH
    "${CMAKE_CURRENT_LIST_DIR}/../cmake_modules"
    "${CMAKE_CURRENT_LIST_DIR}/../DomesdayDuplicator/gui-app/tools/cmake_modules"
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(LibUSB REQUIRED)
include(CTest)

set(GUI_CAPTURE_DIR "${CMAKE_CURRENT_LIST_DIR}/../DomesdayDuplicator/gui-app/tools/DomesdayDuplicator")

add_executable(dddcli
    src/main.cpp
    src/CliConfig.cpp
    src/ConsoleLogger.cpp
    src/CaptureMetadata.cpp
    src/PlayerSerial.cpp
    "${GUI_CAPTURE_DIR}/UsbDeviceBase.cpp"
    "${GUI_CAPTURE_DIR}/UsbDeviceLibUsb.cpp"
)

target_include_directories(dddcli PRIVATE
    src
    "${GUI_CAPTURE_DIR}"
    ${LibUSB_INCLUDE_DIRS}
)

target_link_libraries(dddcli PRIVATE
    ${LibUSB_LIBRARIES}
    pthread
)

install(TARGETS dddcli)

if(BUILD_TESTING)
    add_executable(dddcli_tests
        tests/CliConfigTests.cpp
        src/CliConfig.cpp
        src/PlayerSerial.cpp
    )
    target_include_directories(dddcli_tests PRIVATE
        src
        "${GUI_CAPTURE_DIR}"
    )
    add_test(NAME dddcli_tests COMMAND dddcli_tests)
endif()
