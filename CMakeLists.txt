# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(hello_world)

target_sources(app PRIVATE src/consts.h src/main.c src/callbacks.c src/callbacks.h src/temperatureMonitor.c src/temperatureMonitor.h src/hardware.c src/hardware.h)
