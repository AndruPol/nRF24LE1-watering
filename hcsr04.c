/*
 * hcsr04.c
 *
 *  Created on: 11 июля 2016 г.
 *      Author: andru
 */

#define T0MAX		28000	// > 38ms
#define TRIGUS		26		// TRIG pin pulse = 11uS

#define TRIGPIN		GPIO_PIN_ID_P1_5		// P1.5 - номер пина TRIG датчика HC-SR04
#define IFPPIN		GPIO_PIN_ID_P0_6		// P0.6 - номер пина ECHO датчика HC-SR04

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "delay.h"
#include "gpio.h"
#include "timer0.h"
#include "interrupt.h"
#include "uart.h"

#include "hcsr04.h"

// IFP interrupt handler - not fired?
interrupt_isr_ifp() {
	timer0_stop();
}

// Timer0 interrupt handler - not fired?
interrupt_isr_t0() {
	timer0_stop();
	timer0_set_t0_val(T0MAX);
}

void  hcsr04_init(void) {

	// TRIG pin configure
	gpio_pin_configure(TRIGPIN,
			GPIO_PIN_CONFIG_OPTION_DIR_OUTPUT |
			GPIO_PIN_CONFIG_OPTION_OUTPUT_VAL_SET |
			GPIO_PIN_CONFIG_OPTION_PIN_MODE_OUTPUT_BUFFER_NORMAL_DRIVE_STRENGTH
			);

	// ECHO pin configure
	gpio_pin_configure(IFPPIN,
			GPIO_PIN_CONFIG_OPTION_DIR_INPUT |
			GPIO_PIN_CONFIG_OPTION_PIN_MODE_INPUT_BUFFER_OFF
			);

	interrupt_control_t0_enable();

	interrupt_configure_ifp(
			INTERRUPT_IFP_INPUT_GPINT1,				// IFP = P0.6
			INTERRUPT_IFP_CONFIG_OPTION_ENABLE |
			INTERRUPT_IFP_CONFIG_OPTION_TYPE_FALLING_EDGE
			);

	timer0_configure(
			TIMER0_CONFIG_OPTION_MODE_1_16_BIT_CTR_TMR |
			TIMER0_CONFIG_OPTION_FUNCTION_TIMER |
			TIMER0_CONFIG_OPTION_GATE_RUN_TIMER_WHEN_IFP_HIGH,
			T0MAX
			);

}

hcsr04_state_t hcsr04_read(uint16_t *range) {

	uint8_t count;
	uint16_t timeout, _t0val;

	_t0val = 0;
	timeout = 3801;				// 38mS / 10uS + 1
	timer0_set_t0_val(0);

	// 10uS pulse on TRIG pin
	gpio_pin_val_clear(TRIGPIN);
	for (count = 0; count < TRIGUS; count++) {
		nop();
	}
	gpio_pin_val_set(TRIGPIN);

	interrupt_control_ifp_enable();
	timer0_run();

#if DEBUG
	printf("1: TCON:%x, TMOD:%x, T0: %x\r\n", TCON, TMOD, (uint16_t) T0);
#endif

	// waiting for ifp, timer0 interrupt ~38mS
	while (timer0_is_running() && timeout-- > 1) {
		delay_us(10);
	}

	if (timer0_is_running()) {
		timer0_stop();
	}

	interrupt_control_ifp_disable();

	_t0val = (uint16_t) T0;
#if DEBUG
	printf("2: TCON:%x, TMOD:%x, T0: %x\r\n", TCON, TMOD, (uint16_t) T0);
#endif
	if (timeout == 0 && _t0val == 0)
		return HCSR04_TIMEOUT;

	if (_t0val >= T0MAX)
		return HCSR04_MAX;

#if DEBUG
	if (timer0_has_overflown()) {
		timer0_clear_overflow_flag();
	}

	printf("IP0: %x, IP1:%x, IEN0:%x, IEN1:%x\r\n", IP0, IP1, IEN0, IEN1);
#endif

	*range = (uint16_t) ((uint32_t) _t0val * 30 / 232);	// range, (mm): T0 * 3/4(uS) / 58 * 10

	return HCSR04_OK;

}
