#pragma once
#include <stdint.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>

void callbacks__temperatureReadTimerHandler(struct k_timer *dummy);
void callbacks__temperatureReadHandler(struct k_work *work);
void callbacks__timerResetHandler(struct k_work *work);
void callbacks__buttonPressedHandler(const struct device *dev, struct gpio_callback *cb, uint32_t pins);