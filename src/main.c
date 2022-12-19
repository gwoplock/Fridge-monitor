#include "zephyr/sys/printk.h"
#include <stdbool.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/gpio.h>

void temp_read_timer_handler(struct k_timer *dummy);
void temp_read_handler(struct k_work *work);
void timer_reset_handler(struct k_work *work);
void button_pressed_handler(const struct device *dev, struct gpio_callback *cb, uint32_t pins);

static const struct device *fridge_temp_sense_dev;
static const struct device *freezer_temp_sense_dev;
static const struct gpio_dt_spec timer_reset_button = GPIO_DT_SPEC_GET(DT_NODELABEL(button0), gpios);
static const struct gpio_dt_spec overtemp_led = GPIO_DT_SPEC_GET(DT_NODELABEL(led0), gpios);

static const double fridge_target_temp = 4.0;
static const double freezer_target_temp = 0.0;
static const uint16_t timer_timeout_seconds = 1;
static const uint16_t average_temp_period_seconds = 300;
static const double average_temp_period = average_temp_period_seconds / timer_timeout_seconds;

K_TIMER_DEFINE(temp_read_timer, temp_read_timer_handler, NULL);
K_WORK_DEFINE(temp_read_work_queue, temp_read_handler);
static struct gpio_callback button_cb_data;
K_WORK_DEFINE(timer_reset_work_queue, timer_reset_handler);

K_MUTEX_DEFINE(accounting_vars_mutex);
static uint16_t read_count = 0;
static uint64_t fridge_time_over_temp = 0;
static uint64_t freezer_time_over_temp = 0;

K_MUTEX_DEFINE(average_fridge_temp_mutex);
static double average_fridge_temp = fridge_target_temp;

K_MUTEX_DEFINE(average_freezer_temp_mutex);
static double average_freezer_temp = freezer_target_temp;

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
		fridge_temp_sense_dev = tmp;
	} else {
		k_panic();
	}
}

void getFreezerBme680() {
	static const struct device *tmp = DEVICE_DT_GET(DT_NODELABEL(freezer_temp));
	if (verifyDevice(tmp)){
		freezer_temp_sense_dev = tmp;
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
	if (overtemp_led.port && !device_is_ready(overtemp_led.port)) {
		printk("Error %d: LED device %s is not ready; ignoring it\n",
		       ret, overtemp_led.port->name);
	}
	if (overtemp_led.port) {
		ret = gpio_pin_configure_dt(&overtemp_led, GPIO_OUTPUT);
		if (ret != 0) {
			printk("Error %d: failed to configure LED device %s pin %d\n",
			       ret, overtemp_led.port->name, overtemp_led.pin);
		} else {
			printk("Set up LED at %s pin %d\n", overtemp_led.port->name, overtemp_led.pin);
		}
	}

}

void temp_read_timer_handler(struct k_timer *dummy){
    k_work_submit(&temp_read_work_queue);
}

void temp_read_handler(struct k_work *work) {
	struct sensor_value fridge_temp; 
	struct sensor_value freezer_temp;

	sensor_sample_fetch(fridge_temp_sense_dev);
	sensor_sample_fetch(freezer_temp_sense_dev);
	sensor_channel_get(fridge_temp_sense_dev, SENSOR_CHAN_AMBIENT_TEMP, &fridge_temp);
	sensor_channel_get(freezer_temp_sense_dev, SENSOR_CHAN_AMBIENT_TEMP, &freezer_temp);
	
	double fridge_temp_d = sensor_value_to_double(&fridge_temp);
	double freezer_temp_d = sensor_value_to_double(&freezer_temp);

	k_mutex_lock(&average_fridge_temp_mutex, K_MSEC(100));
	average_fridge_temp = ((average_fridge_temp * (average_temp_period - 1)) + fridge_temp_d) / average_temp_period;
	k_mutex_unlock(&average_fridge_temp_mutex);

	k_mutex_lock(&average_freezer_temp_mutex, K_MSEC(100));
	average_freezer_temp = ((average_freezer_temp * (average_temp_period - 1)) + freezer_temp_d) / average_temp_period;
	k_mutex_unlock(&average_freezer_temp_mutex);


	k_mutex_lock(&accounting_vars_mutex, K_MSEC(100));
	read_count++;
	if (read_count >= average_temp_period){
		read_count = 0;

		bool both_undertemp = true;
		k_mutex_lock(&average_fridge_temp_mutex, K_MSEC(100));
		printk("fridge: Current: %f, avg: %f\n", fridge_temp_d, average_fridge_temp);
		if (average_fridge_temp > fridge_target_temp) {
			printk("frige temp too high (target: %f)\n", fridge_target_temp);
			fridge_time_over_temp += average_temp_period_seconds;
			both_undertemp = false;
		} 
		k_mutex_unlock(&average_fridge_temp_mutex);

		k_mutex_lock(&average_freezer_temp_mutex, K_MSEC(100));
		printk("freezer: Current: %f, avg: %f\n", freezer_temp_d, average_freezer_temp);
		if (average_freezer_temp > freezer_target_temp) {	
			printk("freezer temp too high (target: %f)\n", freezer_target_temp);
			freezer_time_over_temp += average_temp_period_seconds;
			both_undertemp = false;
		} 
		k_mutex_unlock(&average_freezer_temp_mutex);

		gpio_pin_set_dt(&overtemp_led, !both_undertemp);

		printk("frige time overtemp: %lld s, freezer time overtemp: %lld s\n", fridge_time_over_temp, freezer_time_over_temp);

	}
	k_mutex_unlock(&accounting_vars_mutex);
}

void button_pressed_handler(const struct device *dev, struct gpio_callback *cb, uint32_t pins) {
	printk("Button pressed at %" PRIu32 "\n", k_cycle_get_32());
	k_work_submit(&timer_reset_work_queue);
}


void timer_reset_handler(struct k_work *work) {
	k_mutex_lock(&accounting_vars_mutex, K_MSEC(100));
	fridge_time_over_temp = 0;
	freezer_time_over_temp = 0;
	read_count = 0;
	k_mutex_unlock(&accounting_vars_mutex);
	gpio_pin_set_dt(&overtemp_led, false);
}

void main(void) {
	printk("Hello World! %s\n", CONFIG_BOARD);

	getBme680s();
	setupButton();
	setupLed();
	
	k_timer_start(&temp_read_timer, K_SECONDS(timer_timeout_seconds), K_SECONDS(timer_timeout_seconds));
}
