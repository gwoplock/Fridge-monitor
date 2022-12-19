#include "zephyr/sys/printk.h"
#include <stdbool.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/gpio.h>

void temperature_read_timer_handler(struct k_timer *dummy);
void temperature_read_handler(struct k_work *work);
void timer_reset_handler(struct k_work *work);
void button_pressed_handler(const struct device *dev, struct gpio_callback *cb, uint32_t pins);

static const struct device *fridge_temperature_sense_dev;
static const struct device *freezer_temperature_sense_dev;
static const struct gpio_dt_spec timer_reset_button = GPIO_DT_SPEC_GET(DT_NODELABEL(button0), gpios);
static const struct gpio_dt_spec over_temperature_led = GPIO_DT_SPEC_GET(DT_NODELABEL(led0), gpios);

static const double fridge_target_temperature = 4.0;
static const double freezer_target_temperature = 0.0;
static const uint16_t timer_timeout_seconds = 1;
static const uint16_t average_temperature_period_seconds = 300;
static const double average_temperature_period = average_temperature_period_seconds / timer_timeout_seconds;

K_TIMER_DEFINE(temperature_read_timer, temperature_read_timer_handler, NULL);
K_WORK_DEFINE(temperature_read_work_queue, temperature_read_handler);
static struct gpio_callback button_cb_data;
K_WORK_DEFINE(timer_reset_work_queue, timer_reset_handler);

K_MUTEX_DEFINE(accounting_vars_mutex);
static uint16_t read_count = 0;
static uint64_t fridge_time_over_temperature = 0;
static uint64_t freezer_time_over_temperature = 0;

K_MUTEX_DEFINE(average_fridge_temperature_mutex);
static double average_fridge_temperature = fridge_target_temperature;

K_MUTEX_DEFINE(average_freezer_temperature_mutex);
static double average_freezer_temperature = freezer_target_temperature;

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
		fridge_temperature_sense_dev = tmp;
	} else {
		k_panic();
	}
}

void getFreezerBme680() {
	static const struct device *tmp = DEVICE_DT_GET(DT_NODELABEL(freezer_temp));
	if (verifyDevice(tmp)){
		freezer_temperature_sense_dev = tmp;
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
	if (!device_is_ready(timer_reset_button.port)) {
		printk("Error: button device %s is not ready\n",
		       timer_reset_button.port->name);
		return;
	}

	ret = gpio_pin_configure_dt(&timer_reset_button, GPIO_INPUT);
	if (ret != 0) {
		printk("Error %d: failed to configure %s pin %d\n",
		       ret, timer_reset_button.port->name, timer_reset_button.pin);
		return;
	}

	ret = gpio_pin_interrupt_configure_dt(&timer_reset_button,
					      GPIO_INT_EDGE_TO_ACTIVE);
	if (ret != 0) {
		printk("Error %d: failed to configure interrupt on %s pin %d\n",
			ret, timer_reset_button.port->name, timer_reset_button.pin);
		return;
	}

	gpio_init_callback(&button_cb_data, button_pressed_handler, BIT(timer_reset_button.pin));
	gpio_add_callback(timer_reset_button.port, &button_cb_data);
	printk("Set up button at %s pin %d\n", timer_reset_button.port->name, timer_reset_button.pin);
}

void setupLed(){
	int ret;
	if (over_temperature_led.port && !device_is_ready(over_temperature_led.port)) {
		printk("Error %d: LED device %s is not ready; ignoring it\n",
		       ret, over_temperature_led.port->name);
	}
	if (over_temperature_led.port) {
		ret = gpio_pin_configure_dt(&over_temperature_led, GPIO_OUTPUT);
		if (ret != 0) {
			printk("Error %d: failed to configure LED device %s pin %d\n",
			       ret, over_temperature_led.port->name, over_temperature_led.pin);
		} else {
			printk("Set up LED at %s pin %d\n", over_temperature_led.port->name, over_temperature_led.pin);
		}
	}

}

void temperature_read_timer_handler(struct k_timer *dummy){
    k_work_submit(&temperature_read_work_queue);
}

void temperature_read_handler(struct k_work *work) {
	struct sensor_value fridge_temp; 
	struct sensor_value freezer_temp;

	sensor_sample_fetch(fridge_temperature_sense_dev);
	sensor_sample_fetch(freezer_temperature_sense_dev);
	sensor_channel_get(fridge_temperature_sense_dev, SENSOR_CHAN_AMBIENT_TEMP, &fridge_temp);
	sensor_channel_get(freezer_temperature_sense_dev, SENSOR_CHAN_AMBIENT_TEMP, &freezer_temp);
	
	double fridge_temp_d = sensor_value_to_double(&fridge_temp);
	double freezer_temp_d = sensor_value_to_double(&freezer_temp);

	k_mutex_lock(&average_fridge_temperature_mutex, K_MSEC(100));
	average_fridge_temperature = ((average_fridge_temperature * (average_temperature_period - 1)) + fridge_temp_d) / average_temperature_period;
	k_mutex_unlock(&average_fridge_temperature_mutex);

	k_mutex_lock(&average_freezer_temperature_mutex, K_MSEC(100));
	average_freezer_temperature = ((average_freezer_temperature * (average_temperature_period - 1)) + freezer_temp_d) / average_temperature_period;
	k_mutex_unlock(&average_freezer_temperature_mutex);


	k_mutex_lock(&accounting_vars_mutex, K_MSEC(100));
	read_count++;
	if (read_count >= average_temperature_period){
		read_count = 0;

		bool both_undertemp = true;
		k_mutex_lock(&average_fridge_temperature_mutex, K_MSEC(100));
		printk("fridge: Current: %f, avg: %f\n", fridge_temp_d, average_fridge_temperature);
		if (average_fridge_temperature > fridge_target_temperature) {
			printk("frige temp too high (target: %f)\n", fridge_target_temperature);
			fridge_time_over_temperature += average_temperature_period_seconds;
			both_undertemp = false;
		} 
		k_mutex_unlock(&average_fridge_temperature_mutex);

		k_mutex_lock(&average_freezer_temperature_mutex, K_MSEC(100));
		printk("freezer: Current: %f, avg: %f\n", freezer_temp_d, average_freezer_temperature);
		if (average_freezer_temperature > freezer_target_temperature) {	
			printk("freezer temp too high (target: %f)\n", freezer_target_temperature);
			freezer_time_over_temperature += average_temperature_period_seconds;
			both_undertemp = false;
		} 
		k_mutex_unlock(&average_freezer_temperature_mutex);

		gpio_pin_set_dt(&over_temperature_led, !both_undertemp);

		printk("frige time overtemp: %lld s, freezer time overtemp: %lld s\n", fridge_time_over_temperature, freezer_time_over_temperature);

	}
	k_mutex_unlock(&accounting_vars_mutex);
}

void button_pressed_handler(const struct device *dev, struct gpio_callback *cb, uint32_t pins) {
	printk("Button pressed at %" PRIu32 "\n", k_cycle_get_32());
	k_work_submit(&timer_reset_work_queue);
}


void timer_reset_handler(struct k_work *work) {
	k_mutex_lock(&accounting_vars_mutex, K_MSEC(100));
	fridge_time_over_temperature = 0;
	freezer_time_over_temperature = 0;
	read_count = 0;
	k_mutex_unlock(&accounting_vars_mutex);
	gpio_pin_set_dt(&over_temperature_led, false);
}

void main(void) {
	printk("Hello World! %s\n", CONFIG_BOARD);

	getBme680s();
	setupButton();
	setupLed();
	
	k_timer_start(&temperature_read_timer, K_SECONDS(timer_timeout_seconds), K_SECONDS(timer_timeout_seconds));
}
