#include "app_config.h"
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

/*
  0 1 2 3 4 5 6 7 8 9 a b c d e f
0   1 2 3 4 5 6 7 8 9 0 -     Ctrl
1   Q W E R T Y U I O P [   LSuper
2   A S D F G H J K L ; '       rAlt
3   Z X C V B N M , . /Up     Power
4Esf1f2f3f4f5f6f7f8f9fAfBlS
5                   ] \ Return  lAlt
6                      DlrS
7Sp `            LfDnEnRt     RSuper
  0 1 2 3 4 5 6 7 8 9 a b c d e f
*/

static const uint8_t kbd_entries[NUM_OF_ROWS][NUM_OF_COLS] =
{ {0, 0, 0, 0, KEY_ESC, 0, 0, KEY_SPACE}
, {KEY_1, KEY_Q, KEY_A, KEY_Z, KEY_F1, 0, 0, KEY_GRAVE}
, {KEY_2, KEY_W, KEY_S, KEY_X, KEY_F2, 0, 0, 0}
, {KEY_3, KEY_E, KEY_D, KEY_C, KEY_F3, 0, 0, 0}
, {KEY_4, KEY_R, KEY_F, KEY_V, KEY_F4, 0, 0, 0}
, {KEY_5, KEY_T, KEY_G, KEY_B, KEY_F5, 0, 0, 0}
, {KEY_6, KEY_Y, KEY_H, KEY_N, KEY_F6, 0, 0, 0}
, {KEY_7, KEY_U, KEY_J, KEY_M, KEY_F7, 0, 0, 0}
, {KEY_8, KEY_I, KEY_K, KEY_COMMA, KEY_F8, 0, 0, KEY_LEFT}
, {KEY_9, KEY_O, KEY_L, KEY_DOT, KEY_F9, KEY_RIGHTBRACE, 0, KEY_DOWN}
, {KEY_0, KEY_P, KEY_SEMICOLON, KEY_SLASH, KEY_F10, KEY_BACKSLASH, 0, KEY_ENTER}
, {KEY_MINUS, KEY_LEFTBRACE, KEY_APOSTROPHE, KEY_UP, KEY_F11, KEY_ENTER, KEY_BACKSPACE, KEY_RIGHT}
, {0, 0, 0, 0, KEY_LEFTSHIFT, 0, KEY_RIGHTSHIFT, 0}
, {0, KEY_LEFTMETA, 0, 0, 0, 0, 0, 0}
, {KEY_LEFTCTRL, 0, 0, KEY_F13, 0, 0, 0, KEY_RIGHTMETA}
, {0, 0, KEY_RIGHTALT, 0, 0, KEY_LEFTALT, 0, 0}
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
