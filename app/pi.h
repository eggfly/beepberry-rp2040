#pragma once

#include <stdint.h>

enum power_on_reason
{
	POWER_ON_FW_INIT = 0x1, // RP2040 initialized
	POWER_ON_BUTTON = 0x2, // Power button held after Pi was shut down
	POWER_ON_REWAKE = 0x3, // Pi was reawakened from a scheduled timer
	POWER_ON_REWAKE_CANCELED = 0x4 // Rewake timer was canceled
};

#define MINIMUM_SHUTDOWN_GRACE_MS 5000

void pi_power_init(void);
void pi_power_on(enum power_on_reason reason);
void pi_power_off(void);

void pi_schedule_power_on(uint32_t ms);
void pi_schedule_power_off(uint32_t ms);
void pi_cancel_power_alarms();

void led_sync(uint8_t enable);
void led_init(void);
void led_flash(uint8_t enable);
