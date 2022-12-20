#include "callbacks.h"
#include "consts.h"
#include "temperatureMonitor.h"

#include <zephyr/kernel.h>

K_WORK_DEFINE(TEMPERATURE_READ_WORK_QUEUE, callbacks__temperatureReadHandler);
K_WORK_DEFINE(TIMER_RESET_WORK_QUEUE, callbacks__timerResetHandler);

K_MUTEX_DEFINE(ACCOUNTING_VARS_MUTEX);
static uint16_t read_count = 0;

void callbacks__temperatureReadTimerHandler(struct k_timer *dummy){
    k_work_submit(&TEMPERATURE_READ_WORK_QUEUE);
}

void callbacks__temperatureReadHandler(struct k_work *work) {
	temperatureMonitor__updateAverageTemperatures();

	k_mutex_lock(&ACCOUNTING_VARS_MUTEX, K_MSEC(100));
	read_count++;
	if (read_count >= AVERAGE_TEMPERATURE_PERIOD){
		read_count = 0;

		temperatureMonitor__checkForOverTemperature();
	}
	k_mutex_unlock(&ACCOUNTING_VARS_MUTEX);
}

void callbacks__buttonPressedHandler(const struct device *dev, struct gpio_callback *cb, uint32_t pins) {
	printk("Button pressed at %" PRIu32 "\n", k_cycle_get_32());
	k_work_submit(&TIMER_RESET_WORK_QUEUE);
}


void callbacks__timerResetHandler(struct k_work *work) {
	k_mutex_lock(&ACCOUNTING_VARS_MUTEX, K_MSEC(100));
    temperatureMonitor__resetTimers();
	read_count = 0;
	k_mutex_unlock(&ACCOUNTING_VARS_MUTEX);
	gpio_pin_set_dt(&OVER_TEMPERATURE_LED, false);
}