#include "temperatureMonitor.h"
#include "hardware.h"
#include "consts.h"

#include <zephyr/kernel.h>


K_MUTEX_DEFINE(AVERAGE_FRIDGE_TEMPERATURE_MUTEX);
static double average_fridge_temperature = FRIDGE_TARGET_TEMPERATURE;

K_MUTEX_DEFINE(AVERAGE_FREEZER_TEMPERATURE_MUTEX);
static double average_freezer_temperature = FREEZER_TARGET_TEMPERATURE;

K_MUTEX_DEFINE(OVER_TEMPERATURE_TIME_MUTEX);
static uint64_t fridge_time_over_temperature = 0;
static uint64_t freezer_time_over_temperature = 0;

void temperatureMonitor__updateAverageTemperatures() {
	struct sensor_value fridge_temp; 
	struct sensor_value freezer_temp;

	sensor_sample_fetch(FRIDGE_TEMPERATURE_SENSE_DEVICE);
	sensor_sample_fetch(FREEZER_TEMPERATURE_SENSE_DEVICE);
	sensor_channel_get(FRIDGE_TEMPERATURE_SENSE_DEVICE, SENSOR_CHAN_AMBIENT_TEMP, &fridge_temp);
	sensor_channel_get(FREEZER_TEMPERATURE_SENSE_DEVICE, SENSOR_CHAN_AMBIENT_TEMP, &freezer_temp);
	
	double fridge_temp_d = sensor_value_to_double(&fridge_temp);
	double freezer_temp_d = sensor_value_to_double(&freezer_temp);

	k_mutex_lock(&AVERAGE_FRIDGE_TEMPERATURE_MUTEX, K_MSEC(100));
	average_fridge_temperature = ((average_fridge_temperature * (AVERAGE_TEMPERATURE_PERIOD - 1)) + fridge_temp_d) / AVERAGE_TEMPERATURE_PERIOD;
	k_mutex_unlock(&AVERAGE_FRIDGE_TEMPERATURE_MUTEX);

	k_mutex_lock(&AVERAGE_FREEZER_TEMPERATURE_MUTEX, K_MSEC(100));
	average_freezer_temperature = ((average_freezer_temperature * (AVERAGE_TEMPERATURE_PERIOD - 1)) + freezer_temp_d) / AVERAGE_TEMPERATURE_PERIOD;
	k_mutex_unlock(&AVERAGE_FREEZER_TEMPERATURE_MUTEX);
}

void temperatureMonitor__checkForOverTemperature(){
	bool both_under_temp = true;
	k_mutex_lock(&AVERAGE_FRIDGE_TEMPERATURE_MUTEX, K_MSEC(100));
	printk("fridge: avg: %f\n",  average_fridge_temperature);
	if (average_fridge_temperature > FRIDGE_TARGET_TEMPERATURE) {
		printk("fridge temp too high (target: %f)\n", FRIDGE_TARGET_TEMPERATURE);
        k_mutex_lock(&OVER_TEMPERATURE_TIME_MUTEX, K_MSEC(100));
		fridge_time_over_temperature += AVERAGE_TEMPERATURE_PERIOD_SECONDS;
        k_mutex_unlock(&OVER_TEMPERATURE_TIME_MUTEX);
		both_under_temp = false;
	} 
	k_mutex_unlock(&AVERAGE_FRIDGE_TEMPERATURE_MUTEX);

	k_mutex_lock(&AVERAGE_FREEZER_TEMPERATURE_MUTEX, K_MSEC(100));
	printk("freezer: avg: %f\n", average_freezer_temperature);
	if (average_freezer_temperature > FREEZER_TARGET_TEMPERATURE) {	
		printk("freezer temp too high (target: %f)\n", FREEZER_TARGET_TEMPERATURE);
        k_mutex_lock(&OVER_TEMPERATURE_TIME_MUTEX, K_MSEC(100));
		freezer_time_over_temperature += AVERAGE_TEMPERATURE_PERIOD_SECONDS;
        k_mutex_unlock(&OVER_TEMPERATURE_TIME_MUTEX);
		both_under_temp = false;
	} 
	k_mutex_unlock(&AVERAGE_FREEZER_TEMPERATURE_MUTEX);

	gpio_pin_set_dt(&OVER_TEMPERATURE_LED, !both_under_temp);

	printk("fridge time over temp: %lld s, freezer time over temp: %lld s\n", fridge_time_over_temperature, freezer_time_over_temperature);
}

void temperatureMonitor__resetTimers(){

    k_mutex_lock(&OVER_TEMPERATURE_TIME_MUTEX, K_MSEC(100));
    fridge_time_over_temperature = 0;
    freezer_time_over_temperature = 0;
    k_mutex_unlock(&OVER_TEMPERATURE_TIME_MUTEX);
}