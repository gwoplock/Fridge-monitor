#pragma once
#include <stdint.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/gpio.h>

static const struct device *FRIDGE_TEMPERATURE_SENSE_DEVICE;
static const struct device *FREEZER_TEMPERATURE_SENSE_DEVICE;
static const struct gpio_dt_spec TIMER_RESET_BUTTON = GPIO_DT_SPEC_GET(DT_NODELABEL(button0), gpios);
static const struct gpio_dt_spec OVER_TEMPERATURE_LED = GPIO_DT_SPEC_GET(DT_NODELABEL(led0), gpios);

static const double FRIDGE_TARGET_TEMPERATURE = 4.0;
static const double FREEZER_TARGET_TEMPERATURE = 0.0;
static const uint16_t TIMER_TIMEOUT_SECONDS = 1;
static const uint16_t AVERAGE_TEMPERATURE_PERIOD_SECONDS = 300;
static const double AVERAGE_TEMPERATURE_PERIOD = AVERAGE_TEMPERATURE_PERIOD_SECONDS / TIMER_TIMEOUT_SECONDS;