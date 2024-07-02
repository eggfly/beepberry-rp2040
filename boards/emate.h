#pragma once

#define USB_VID				0x1209
#define USB_PID				0xB182
#define USB_PRODUCT			"eMateKbd"

#define PIN_PUPPET_SDA		28
#define PIN_PUPPET_SCL		29

#define NUM_OF_COLS			8
#define PINS_COLS \
	0, \
	1, \
	2, \
	3, \
	4, \
	5, \
	6, \
	7 \

#define NUM_OF_ROWS			16
#define PINS_ROWS \
	8,  \
	9,  \
	10, \
	11, \
	12, \
	13, \
	14, \
	15, \
	16, \
	17, \
	18, \
	19, \
	20, \
	21, \
	22, \
	23

#define PICO_FLASH_SIZE_BYTES (16 * 1024 * 1024)
