#include "app_config.h"
#include "fifo.h"
#include "keyboard.h"
#include "reg.h"

#include <pico/stdlib.h>

// Size of the list keeping track of all the pressed keys
#define MAX_TRACKED_KEYS 10

static struct
{
	struct key_callback *key_callbacks;
} self;

// Key and buttons definitions

static const uint8_t row_pins[NUM_OF_ROWS] =
{
	PINS_ROWS
};

static const uint8_t col_pins[NUM_OF_COLS] =
{
	PINS_COLS
};

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"

static const uint8_t kbd_entries[NUM_OF_ROWS][NUM_OF_COLS] =
//  Touchpad center key
{ { KEY_COMPOSE, KEY_W, KEY_G, KEY_S, KEY_L, KEY_H }
, {         0x0, KEY_Q, KEY_R, KEY_E, KEY_O, KEY_U }
//   Call button
, { /*KEY_OPEN*/0x0, KEY_0, KEY_F, /*KEY_LEFTSHIFT*/0x0, KEY_K, KEY_J }
, {         0x0, KEY_SPACE, KEY_C, KEY_Z, KEY_M, KEY_N }
//    Berry key  Symbol key
, { /*KEY_PROPS*/0x0, /*KEY_RIGHTALT*/0x0, KEY_T, KEY_D, KEY_I, KEY_Y }
//      Back key Alt key
, {     KEY_ESC, /*KEY_LEFTALT*/0x0, KEY_V, KEY_X, KEY_MUTE, KEY_B }
, {         0x0, KEY_A, /*KEY_RIGHTSHIFT*/0x0, KEY_P, KEY_BACKSPACE, KEY_ENTER }
};
static bool kbd_pressed[NUM_OF_ROWS][NUM_OF_COLS] = {};

#pragma GCC diagnostic pop

static void handle_key_event(uint r, uint c, bool pressed)
{
	uint8_t keycode;

	// Don't send unchanged state
	if (pressed == kbd_pressed[r][c]) {
		return;
	}

	// Get keycode
	keycode = kbd_entries[r][c];

	// Don't send disabled keycodes
	if (keycode > 0) {
		keyboard_inject_event(keycode, (pressed)
			? KEY_STATE_PRESSED
			: KEY_STATE_RELEASED);
	}
	kbd_pressed[r][c] = pressed;
}

static int64_t timer_task(alarm_id_t id, void *user_data)
{
	(void)id;
	(void)user_data;
	uint c, r, i;
	bool pressed;

	for (c = 0; c < NUM_OF_COLS; c++) {
		gpio_pull_up(col_pins[c]);
		gpio_put(col_pins[c], 0);
		gpio_set_dir(col_pins[c], GPIO_OUT);

		for (r = 0; r < NUM_OF_ROWS; r++) {
			pressed = (gpio_get(row_pins[r]) == 0);
			handle_key_event(r, c, pressed);
		}

		gpio_put(col_pins[c], 1);
		gpio_disable_pulls(col_pins[c]);
		gpio_set_dir(col_pins[c], GPIO_IN);
	}

	// negative value means interval since last alarm time
	return -(reg_get_value(REG_ID_FRQ) * 1000);
}

void keyboard_inject_event(uint8_t key, enum key_state state)
{
	struct fifo_item item;
	item.scancode = key;
	item.state = state;

	if (!fifo_enqueue(item)) {
		if (reg_is_bit_set(REG_ID_CFG, CFG_OVERFLOW_INT)) {
			reg_set_bit(REG_ID_INT, INT_OVERFLOW);
		}

		if (reg_is_bit_set(REG_ID_CFG, CFG_OVERFLOW_ON)) {
			fifo_enqueue_force(item);
		}
	}

	struct key_callback *cb = self.key_callbacks;
	while (cb) {
		cb->func(key, state);
		cb = cb->next;
	}
}

void keyboard_add_key_callback(struct key_callback *callback)
{
	// first callback
	if (!self.key_callbacks) {
		self.key_callbacks = callback;
		return;
	}

	// find last and insert after
	struct key_callback *cb = self.key_callbacks;
	while (cb->next) {

		// Only add callback once to avoid cycles
		if (cb == callback) {
			return;
		}

		cb = cb->next;
	}
	cb->next = callback;
	callback->next = NULL;
}

void keyboard_remove_key_callback(void *func)
{
	if (self.key_callbacks == NULL) {
		return;
	}

	struct key_callback **cursor = &self.key_callbacks;

	// Find matching and remove
	while (*cursor != NULL) {
		if ((*cursor)->func == func) {
			*cursor = (*cursor)->next;
			break;
		}
		cursor = &((*cursor)->next);
	}
}

void keyboard_init(void)
{
	uint i;

	// GPIO rows
	for (i = 0; i < NUM_OF_ROWS; ++i) {
		gpio_init(row_pins[i]);
		gpio_pull_up(row_pins[i]);
		gpio_set_dir(row_pins[i], GPIO_IN);
	}

	// GPIO columns
	for(i = 0; i < NUM_OF_COLS; ++i) {
		gpio_init(col_pins[i]);
		gpio_set_dir(col_pins[i], GPIO_IN);
	}

	add_alarm_in_ms(reg_get_value(REG_ID_FRQ), timer_task, NULL, true);
}
