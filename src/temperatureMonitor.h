#pragma once
#include <stdint.h>

void temperatureMonitor__checkForOverTemperature();
void temperatureMonitor__updateAverageTemperatures();
void temperatureMonitor__resetTimers();

uint64_t temperatureMonitor__getFridgeTimeOverTemperature();
uint64_t temperatureMonitor__getFreezerTimeOverTemperature();
double temperatureMonitor__getFridgeTemperature();
double temperatureMonitor__getFreezerTemperature();