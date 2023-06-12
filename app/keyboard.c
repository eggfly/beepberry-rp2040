#include "app_config.h"
#include "fifo.h"
#include "keyboard.h"
#include "reg.h"
#include "pi.h"

#include <pico/stdlib.h>

// Size of the list keeping track of all the pressed keys
#define MAX_TRACKED_KEYS 10

typedef struct
{
	char keycode;
	uint32_t hold_start_time;
	enum key_state state;
} tracked_key_t;

static struct
{
	struct key_callback *key_callbacks;
	tracked_key_t tracked_keys[MAX_TRACKED_KEYS];
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
{ { KEY_COMPOSE, KEY_W, KEY_G, KEY_S, KEY_L, KEY_H }
, {         0x0, KEY_Q, KEY_R, KEY_E, KEY_O, KEY_U }
, {    KEY_OPEN, KEY_0, KEY_F, KEY_LEFTSHIFT, KEY_K, KEY_J }
, {         0x0, KEY_SPACE, KEY_C, KEY_Z, KEY_M, KEY_N }
, {   KEY_PROPS, KEY_LEFTMETA, KEY_T, KEY_D, KEY_I, KEY_Y }
, {     KEY_ESC, KEY_LEFTALT, KEY_V, KEY_X, KEY_MUTE, KEY_B }
, {         0x0, KEY_A, KEY_RIGHTSHIFT, KEY_P, KEY_BACKSPACE, KEY_ENTER }
};

#if NUM_OF_BTNS > 0
static const char btn_entries[NUM_OF_BTNS] = { KEY_STOP };

static const uint8_t btn_pins[NUM_OF_BTNS] = { 4 };
#endif

#pragma GCC diagnostic pop

static int64_t release_key(alarm_id_t _, void *keycode)
{
	keyboard_inject_event((char)(uint32_t)keycode, KEY_STATE_RELEASED);

	return 0;
}

static void transition_to(tracked_key_t* key, enum key_state const next_state)
{
	// Transition to next state
	key->state = next_state;

	// Don't send power key over USB
	if ((key->keycode != 0) && (key->keycode != KEY_STOP)) {
		keyboard_inject_event(key->keycode, next_state);
	}
}

static uint32_t key_held_for(tracked_key_t const* key)
{
	return to_ms_since_boot(get_absolute_time()) - key->hold_start_time;
}

static void next_item_state(tracked_key_t* key, bool const pressed)
{
	switch (key->state) {

		// Idle -> Pressed
		case KEY_STATE_IDLE:
			if (pressed) {
				transition_to(key, KEY_STATE_PRESSED);

				// Track hold time for transitioning to Hold and Long Hold states
				key->hold_start_time = to_ms_since_boot(get_absolute_time());
			}
			break;

		// Pressed -> Hold | Released
		case KEY_STATE_PRESSED:

			if (key_held_for(key) > (reg_get_value(REG_ID_HLD) * 10)) {
				transition_to(key, KEY_STATE_HOLD);

			} else if (!pressed) {
				transition_to(key, KEY_STATE_RELEASED);
			}
			break;

		// Hold -> Released | Long Hold
		case KEY_STATE_HOLD:
			if (!pressed) {
				transition_to(key, KEY_STATE_RELEASED);

			} else if (key_held_for(key) > LONG_HOLD_MS) {

				// Special handling for power key
				if(key->keycode == KEY_STOP){
					keyboard_inject_event(KEY_STOP, KEY_STATE_PRESSED);
					add_alarm_in_ms(10, release_key, (void*)KEY_STOP, true);
				}

				transition_to(key, KEY_STATE_LONG_HOLD);
			}
			break;

		// Long Hold -> Released
		case KEY_STATE_LONG_HOLD:
			if (!pressed) {
				transition_to(key, KEY_STATE_RELEASED);
			}
			break;

		// Released -> Idle
		case KEY_STATE_RELEASED:

			// Reset list entry so it can be reused
			key->keycode = 0;

			transition_to(key, KEY_STATE_IDLE);
			break;
	}
}

static void update_tracked_keys(tracked_key_t* keys, char keycode, bool pressed)
{
	// Find active keycode in list
	// Keep track of a free index in the list for a new insertion
	int32_t active_key_idx = -1;
	int32_t free_idx = -1;
	for (int32_t i = 0; i < MAX_TRACKED_KEYS; i++) {

		// Save free index
		if (keys[i].keycode == 0) {
			free_idx = i;
			continue;
		}

		// Found active keycode in the list
		if (keys[i].keycode == keycode) {
			active_key_idx = i;
			break;
		}
	}

	// Pressed, but not in list. Insert new keycode
	if (pressed && (active_key_idx < 0)) {

		// Drop new keycodes if the list is full
		if (free_idx >= 0) {

			// Create new list item in the Idle state
			keys[free_idx].keycode = keycode;
			keys[free_idx].state = KEY_STATE_IDLE;
			// Active index is now the newly-created item
			active_key_idx = free_idx;
		}
	}

	// If we had an active index, update its state
	if (active_key_idx >= 0) {
		next_item_state(&keys[active_key_idx], pressed);
	}
}

static int64_t timer_task(alarm_id_t id, void *user_data)
{
	(void)id;
	(void)user_data;

	for (uint32_t c = 0; c < NUM_OF_COLS; ++c) {
		gpio_pull_up(col_pins[c]);
		gpio_put(col_pins[c], 0);
		gpio_set_dir(col_pins[c], GPIO_OUT);

		for (uint32_t r = 0; r < NUM_OF_ROWS; ++r) {
			const bool pressed = (gpio_get(row_pins[r]) == 0);
			const int32_t key_idx = (int32_t)((r * NUM_OF_COLS) + c);
			update_tracked_keys(self.tracked_keys, kbd_entries[r][c], pressed);
		}

		gpio_put(col_pins[c], 1);
		gpio_disable_pulls(col_pins[c]);
		gpio_set_dir(col_pins[c], GPIO_IN);
	}

#if NUM_OF_BTNS > 0
	for (uint32_t b = 0; b < NUM_OF_BTNS; ++b) {
		const bool pressed = (gpio_get(btn_pins[b]) == 0);
		update_tracked_keys(self.tracked_keys, btn_entries[b], pressed);
	}
#endif

	// negative value means interval since last alarm time
	return -(reg_get_value(REG_ID_FRQ) * 1000);
}

void keyboard_inject_event(char key, enum key_state state)
{
	const struct fifo_item item = { key, state };
	if (!fifo_enqueue(item)) {
		if (reg_is_bit_set(REG_ID_CFG, CFG_OVERFLOW_INT))
			reg_set_bit(REG_ID_INT, INT_OVERFLOW);

		if (reg_is_bit_set(REG_ID_CFG, CFG_OVERFLOW_ON))
			fifo_enqueue_force(item);
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
	while (cb->next)
		cb = cb->next;

	cb->next = callback;
}

void keyboard_init(void)
{
	// rows
	for (uint32_t i = 0; i < NUM_OF_ROWS; ++i) {
		gpio_init(row_pins[i]);
		gpio_pull_up(row_pins[i]);
		gpio_set_dir(row_pins[i], GPIO_IN);
	}

	// cols
	for(uint32_t i = 0; i < NUM_OF_COLS; ++i) {
		gpio_init(col_pins[i]);
		gpio_set_dir(col_pins[i], GPIO_IN);
	}

	// btns
#if NUM_OF_BTNS > 0
	for(uint32_t i = 0; i < NUM_OF_BTNS; ++i) {
		gpio_init(btn_pins[i]);
		gpio_pull_up(btn_pins[i]);
		gpio_set_dir(btn_pins[i], GPIO_IN);
	}
#endif

	add_alarm_in_ms(reg_get_value(REG_ID_FRQ), timer_task, NULL, true);
}
