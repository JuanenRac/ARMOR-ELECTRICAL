# ARMOR-ELECTRICAL host tests: the meters' library, the switching rules and the node settings, built with any C++17 compiler.
# Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
cmake_minimum_required(VERSION 3.16)
project(armor_electrical_host_tests CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_options(-Wall -Wextra -Wpedantic -Werror)

add_executable(test_meters test_meters.cpp)
add_executable(test_interlock test_interlock.cpp)
add_executable(test_config test_config.cpp)
add_executable(test_runner test_runner.cpp)
add_executable(test_ble test_ble.cpp)
add_executable(test_switch_set test_switch_set.cpp)
add_executable(emit_samples emit_samples.cpp)

enable_testing()
add_test(NAME meters COMMAND test_meters)
add_test(NAME interlock COMMAND test_interlock)
add_test(NAME config COMMAND test_config)
add_test(NAME runner COMMAND test_runner)
add_test(NAME ble COMMAND test_ble)
add_test(NAME switch_set COMMAND test_switch_set ${CMAKE_CURRENT_SOURCE_DIR}/../../ARMOR-COMMON/conformance)
