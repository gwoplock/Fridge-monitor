#include "callbacks.h"
#include "hardware.h"
#include "consts.h"


K_TIMER_DEFINE(TEMPERATURE_READ_TIMER, callbacks__temperatureReadTimerHandler, NULL);

void main(void) {
	printk("Hello World! %s\n", CONFIG_BOARD);

	hardware__setup();
	
	k_timer_start(&TEMPERATURE_READ_TIMER, K_SECONDS(TIMER_TIMEOUT_SECONDS), K_SECONDS(TIMER_TIMEOUT_SECONDS));
}
