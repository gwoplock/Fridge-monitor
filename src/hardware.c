#include "hardware.h"
#include "consts.h"
#include "callbacks.h"

#include <stdbool.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/gpio.h>

static struct gpio_callback BUTTON_CALLBACK_DATA;

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

	gpio_init_callback(&BUTTON_CALLBACK_DATA, callbacks__buttonPressedHandler, BIT(TIMER_RESET_BUTTON.pin));
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

void hardware__setup(){
	getBme680s();
	setupButton();
	setupLed();
}