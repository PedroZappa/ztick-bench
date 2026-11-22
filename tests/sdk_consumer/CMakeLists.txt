# SPDX-License-Identifier: MIT
# Scratch consumer used by scripts/check_sdk_consumer.sh to verify BR §17.8:
# a third party can find_package(ztick-sdk) and include the installed headers
# without modifying the ztick-bench repository.
cmake_minimum_required(VERSION 3.16)

project(ztick_sdk_consumer LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(ztick-sdk 1.0.0 REQUIRED)

if(NOT "${ztick-sdk_PROJECT_VERSION}" STREQUAL "0.2.0-dev")
  message(FATAL_ERROR
    "Unexpected informational project version: '${ztick-sdk_PROJECT_VERSION}'")
endif()

add_executable(ztick_sdk_consumer_c consumer.c)
target_link_libraries(ztick_sdk_consumer_c PRIVATE ztick::sdk)

add_executable(ztick_sdk_consumer_cpp consumer.cpp)
target_link_libraries(ztick_sdk_consumer_cpp PRIVATE ztick::sdk)
