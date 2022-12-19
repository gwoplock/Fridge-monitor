#include "zephyr/sys/printk.h"
#include <stdbool.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/gpio.h>

void temperatureReadTimerHandler(struct k_timer *dummy);
void temperatureReadHandler(struct k_work *work);
void timerResetHandler(struct k_work *work);
void buttonPressedHandler(const struct device *dev, struct gpio_callback *cb, uint32_t pins);

static const struct device *FRIDGE_TEMPERATURE_SENSE_DEVICE;
static const struct device *FREEZER_TEMPERATURE_SENSE_DEVICE;
static const struct gpio_dt_spec TIMER_RESET_BUTTON = GPIO_DT_SPEC_GET(DT_NODELABEL(button0), gpios);
static const struct gpio_dt_spec OVER_TEMPERATURE_LED = GPIO_DT_SPEC_GET(DT_NODELABEL(led0), gpios);

static const double FRIDGE_TARGET_TEMPERATURE = 4.0;
static const double FREEZER_TARGET_TEMPERATURE = 0.0;
static const uint16_t TIMER_TIMEOUT_SECONDS = 1;
static const uint16_t AVERAGE_TEMPERATURE_PERIOD_SECONDS = 300;
static const double AVERAGE_TEMPERATURE_PERIOD = AVERAGE_TEMPERATURE_PERIOD_SECONDS / TIMER_TIMEOUT_SECONDS;

K_TIMER_DEFINE(TEMPERATURE_READ_TIMER, temperatureReadTimerHandler, NULL);
K_WORK_DEFINE(TEMPERATURE_READ_WORK_QUEUE, temperatureReadHandler);
K_WORK_DEFINE(TIMER_RESET_WORK_QUEUE, timerResetHandler);

static struct gpio_callback BUTTON_CALLBACK_DATA;

K_MUTEX_DEFINE(ACCOUNTING_VARS_MUTEX);
static uint16_t read_count = 0;
static uint64_t fridge_time_over_temperature = 0;
static uint64_t freezer_time_over_temperature = 0;

K_MUTEX_DEFINE(AVERAGE_FRIDGE_TEMPERATURE_MUTEX);
static double average_fridge_temperature = FRIDGE_TARGET_TEMPERATURE;

K_MUTEX_DEFINE(AVERAGE_FREEZER_TEMPERATURE_MUTEX);
static double average_freezer_temperature = FREEZER_TARGET_TEMPERATURE;

bool verifyDevice(const struct device *dev) {
		if (dev == NULL) {
		/* No such node, or the node does not have status "okay". */
		printk("\nError: no device found.\n");
		return false;
	}

	if (!device_is_ready(dev)) {

		printk("\nError: Device \"%s\" is not ready; "
		       "check the driver initialization logs for errors.\n",
		       dev->name);
		return false;
	}

	printk("Device \"%s\" is ready\n", dev->name);
	return true;
}

void getFridgeBme680() {
	static const struct device *tmp = DEVICE_DT_GET(DT_NODELABEL(fridge_temp));
	if (verifyDevice(tmp)){
		FRIDGE_TEMPERATURE_SENSE_DEVICE = tmp;
	} else {
		k_panic();
	}
}

void getFreezerBme680() {
	static const struct device *tmp = DEVICE_DT_GET(DT_NODELABEL(freezer_temp));
	if (verifyDevice(tmp)){
		FREEZER_TEMPERATURE_SENSE_DEVICE = tmp;
	} else {
		k_panic();
	}
}

void getBme680s(){
	getFridgeBme680();
	getFreezerBme680();
}


void setupButton(){
	int ret;
	if (!device_is_ready(TIMER_RESET_BUTTON.port)) {
		printk("Error: button device %s is not ready\n",
		       TIMER_RESET_BUTTON.port->name);
		return;
	}

	ret = gpio_pin_configure_dt(&TIMER_RESET_BUTTON, GPIO_INPUT);
	if (ret != 0) {
		printk("Error %d: failed to configure %s pin %d\n",
		       ret, TIMER_RESET_BUTTON.port->name, TIMER_RESET_BUTTON.pin);
		return;
	}

	ret = gpio_pin_interrupt_configure_dt(&TIMER_RESET_BUTTON,
					      GPIO_INT_EDGE_TO_ACTIVE);
	if (ret != 0) {
		printk("Error %d: failed to configure interrupt on %s pin %d\n",
			ret, TIMER_RESET_BUTTON.port->name, TIMER_RESET_BUTTON.pin);
		return;
	}

	gpio_init_callback(&BUTTON_CALLBACK_DATA, buttonPressedHandler, BIT(TIMER_RESET_BUTTON.pin));
	gpio_add_callback(TIMER_RESET_BUTTON.port, &BUTTON_CALLBACK_DATA);
	printk("Set up button at %s pin %d\n", TIMER_RESET_BUTTON.port->name, TIMER_RESET_BUTTON.pin);
}

void setupLed(){
	int ret;
	if (OVER_TEMPERATURE_LED.port && !device_is_ready(OVER_TEMPERATURE_LED.port)) {
		printk("Error %d: LED device %s is not ready; ignoring it\n",
		       ret, OVER_TEMPERATURE_LED.port->name);
	}
	if (OVER_TEMPERATURE_LED.port) {
		ret = gpio_pin_configure_dt(&OVER_TEMPERATURE_LED, GPIO_OUTPUT);
		if (ret != 0) {
			printk("Error %d: failed to configure LED device %s pin %d\n",
			       ret, OVER_TEMPERATURE_LED.port->name, OVER_TEMPERATURE_LED.pin);
		} else {
			printk("Set up LED at %s pin %d\n", OVER_TEMPERATURE_LED.port->name, OVER_TEMPERATURE_LED.pin);
		}
	}

}

void temperatureReadTimerHandler(struct k_timer *dummy){
    k_work_submit(&TEMPERATURE_READ_WORK_QUEUE);
}

void updateAverageTemperatures() {
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

void checkForOverTemperature(){
	bool both_under_temp = true;
	k_mutex_lock(&AVERAGE_FRIDGE_TEMPERATURE_MUTEX, K_MSEC(100));
	printk("fridge: avg: %f\n",  average_fridge_temperature);
	if (average_fridge_temperature > FRIDGE_TARGET_TEMPERATURE) {
		printk("fridge temp too high (target: %f)\n", FRIDGE_TARGET_TEMPERATURE);
		fridge_time_over_temperature += AVERAGE_TEMPERATURE_PERIOD_SECONDS;
		both_under_temp = false;
	} 
	k_mutex_unlock(&AVERAGE_FRIDGE_TEMPERATURE_MUTEX);

	k_mutex_lock(&AVERAGE_FREEZER_TEMPERATURE_MUTEX, K_MSEC(100));
	printk("freezer: avg: %f\n", average_freezer_temperature);
	if (average_freezer_temperature > FREEZER_TARGET_TEMPERATURE) {	
		printk("freezer temp too high (target: %f)\n", FREEZER_TARGET_TEMPERATURE);
		freezer_time_over_temperature += AVERAGE_TEMPERATURE_PERIOD_SECONDS;
		both_under_temp = false;
	} 
	k_mutex_unlock(&AVERAGE_FREEZER_TEMPERATURE_MUTEX);

	gpio_pin_set_dt(&OVER_TEMPERATURE_LED, !both_under_temp);

	printk("fridge time over temp: %lld s, freezer time over temp: %lld s\n", fridge_time_over_temperature, freezer_time_over_temperature);
}

void temperatureReadHandler(struct k_work *work) {
	updateAverageTemperatures();

	k_mutex_lock(&ACCOUNTING_VARS_MUTEX, K_MSEC(100));
	read_count++;
	if (read_count >= AVERAGE_TEMPERATURE_PERIOD){
		read_count = 0;

		checkForOverTemperature();
	}
	k_mutex_unlock(&ACCOUNTING_VARS_MUTEX);
}

void buttonPressedHandler(const struct device *dev, struct gpio_callback *cb, uint32_t pins) {
	printk("Button pressed at %" PRIu32 "\n", k_cycle_get_32());
	k_work_submit(&TIMER_RESET_WORK_QUEUE);
}


void timerResetHandler(struct k_work *work) {
	k_mutex_lock(&ACCOUNTING_VARS_MUTEX, K_MSEC(100));
	fridge_time_over_temperature = 0;
	freezer_time_over_temperature = 0;
	read_count = 0;
	k_mutex_unlock(&ACCOUNTING_VARS_MUTEX);
	gpio_pin_set_dt(&OVER_TEMPERATURE_LED, false);
}

void setupHardware(){
	getBme680s();
	setupButton();
	setupLed();
}

void main(void) {
	printk("Hello World! %s\n", CONFIG_BOARD);

	setupHardware();
	
	k_timer_start(&TEMPERATURE_READ_TIMER, K_SECONDS(TIMER_TIMEOUT_SECONDS), K_SECONDS(TIMER_TIMEOUT_SECONDS));
}
