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

static const uint8_t kbd_entries[NUM_OF_ROWS][NUM_OF_COLS] =
{ {0,0,0,0,0,0,0,0}
, {0,0,0,0,KEY_DELETE,0,0,0}
, {0,0,0,0,0,0,0,0}
, {0,0,0,0,0,0,0,0}
, {KEY_RIGHT,KEY_BACKSPACE,KEY_ENTER,KEY_INSERT,KEY_UP,KEY_APOSTROPHE,KEY_LEFTBRACE,KEY_MINUS}
, {0,0,KEY_BACKSLASH,KEY_F10,KEY_SLASH,KEY_SEMICOLON,KEY_P,KEY_0}
, {KEY_DOWN,KEY_EQUAL,KEY_RIGHTBRACE,KEY_F9,KEY_DOT,KEY_L,KEY_O,KEY_9}
, {KEY_LEFT,0,0,KEY_F8,KEY_COMMA,KEY_K,KEY_I,KEY_8}
, {0,0,0,KEY_F7,KEY_M,KEY_J,KEY_U,KEY_7}
, {0,0,0,KEY_F6,KEY_N,KEY_H,KEY_Y,KEY_6}
, {0,0,0,KEY_F5,KEY_B,KEY_G,KEY_T,KEY_5}
, {0,0,0,KEY_F4,KEY_V,KEY_F,KEY_R,KEY_4}
, {0,0,0,KEY_F3,KEY_C,KEY_D,KEY_E,KEY_3}
, {0,0,0,KEY_F2,KEY_X,KEY_S,KEY_W,KEY_2}
, {KEY_GRAVE,0,0,KEY_F1,KEY_Z,KEY_A,KEY_Q,KEY_1}
, {KEY_SPACE,0,0,KEY_ESC,0,KEY_CAPSLOCK,KEY_TAB,0}
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
	if (true) {//keycode > 0) {
		keyboard_inject_event((r << 4) + c, (pressed)
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

#if 1
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
#else
	for (r = 0; r < NUM_OF_ROWS; r++) {
		gpio_pull_up(row_pins[r]);
		gpio_put(row_pins[r], 0);
		gpio_set_dir(row_pins[r], GPIO_OUT);

		for (c = 0; c < NUM_OF_COLS; c++) {

			pressed = (gpio_get(col_pins[c]) == 0);
			handle_key_event(r, c, pressed);
		}

		gpio_put(row_pins[r], 1);
		gpio_disable_pulls(row_pins[r]);
		gpio_set_dir(row_pins[r], GPIO_IN);
	}
#endif
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
		#if 1
		gpio_pull_up(row_pins[i]);
		gpio_set_dir(row_pins[i], GPIO_IN);
		#else
		gpio_set_dir(row_pins[i], GPIO_IN);
		#endif
	}

	// GPIO columns
	for(i = 0; i < NUM_OF_COLS; ++i) {
		#if 1
		gpio_init(col_pins[i]);
		gpio_set_dir(col_pins[i], GPIO_IN);
		#else
		gpio_pull_up(col_pins[i]);
		gpio_set_dir(col_pins[i], GPIO_IN);
		#endif
	}

	add_alarm_in_ms(reg_get_value(REG_ID_FRQ), timer_task, NULL, true);
}
