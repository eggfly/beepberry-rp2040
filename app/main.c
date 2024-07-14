#include <pico/stdlib.h>
#include <stdio.h>
#include <tusb.h>

#include <hardware/rtc.h>

#include "hardware/uart.h"
#include "hardware/clocks.h"
#include "hardware/rosc.h"
#include "hardware/structs/scb.h"

#include "debug.h"
#include "keyboard.h"
#include "puppet_i2c.h"
#include "reg.h"
#include "usb.h"

#include "pico/sleep.h"

int main(void)
{
#ifdef DEBUG
	debug_init();
#endif

	// Allow pins 0 and 1 for GPIO keyboard
	uart_deinit(uart0);
	uart_deinit(uart1);

	reg_init();

	keyboard_init();

	puppet_i2c_init();

	usb_init();

#ifndef NDEBUG
	printf("Starting main loop\r\n");
#endif

	while (true) {
		__wfe();
	}

	return 0;
}
