/* File: main.cpp
 * Contains base main function and usually all the other stuff that avr does...
 */
/* Copyright (c) 2012-2013 Domen Ipavec (domen.ipavec@z-v.si)
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation files
 * (the "Software"), to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify, merge,
 * publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#define F_CPU 8000000UL  // 8 MHz
//#include <util/delay.h>

#include <avr/io.h>
#include <avr/interrupt.h>
//#include <avr/pgmspace.h>
//#include <avr/eeprom.h>

#include <stdint.h>

#include "bitop.h"

static const uint8_t I2C_ADDRESS = 0x40;

static volatile uint8_t n = 33;

static void startUtrinek() {
	OCR0B = BIT(7);
	OCR1A = 0x0100;
	n = 0;
}

static const uint8_t LED_ENABLE_PIN = PA7;
static const uint8_t LED_LATCH_PIN = PA1;
static const uint8_t LED_CLOCK_PIN = PA2;
static const uint8_t LED_DATA_PIN = PA3;
static volatile uint8_t * const LED_ENABLE_PORT = &PORTA;
static volatile uint8_t * const LED_LATCH_PORT = &PORTA;
static volatile uint8_t * const LED_CLOCK_PORT = &PORTA;
static volatile uint8_t * const LED_DATA_PORT = &PORTA;
static volatile uint8_t * const LED_ENABLE_DDR = &DDRA;
static volatile uint8_t * const LED_LATCH_DDR = &DDRA;
static volatile uint8_t * const LED_CLOCK_DDR = &DDRA;
static volatile uint8_t * const LED_DATA_DDR = &DDRA;

static inline void led_toggle_clock() {
	SETBIT(*LED_CLOCK_PORT, LED_CLOCK_PIN);
	CLEARBIT(*LED_CLOCK_PORT, LED_CLOCK_PIN);
}

static inline void led_toggle_latch() {
	SETBIT(*LED_LATCH_PORT, LED_LATCH_PIN);
	CLEARBIT(*LED_LATCH_PORT, LED_LATCH_PIN);
}

int main() {
	// init
	SETBIT(*LED_ENABLE_DDR, LED_ENABLE_PIN);
	SETBIT(*LED_LATCH_DDR, LED_LATCH_PIN);
	SETBIT(*LED_CLOCK_DDR, LED_CLOCK_PIN);
	SETBIT(*LED_DATA_DDR, LED_DATA_PIN);

	// timer 1 for advancing led
	OCR1A = 0x0100;
	TCNT1 = 0;
	TCCR1B = 0b00001101;
	SETBIT(TIMSK1, OCIE1A);

	// timer 0 for pwm
	TCCR0A = 0b00110001;
	TCCR0B = 0b010;
	TOCPMCOE = 0b01000000;

	// init i2c
	TWSCRA = 0b00111000;
	TWSA = I2C_ADDRESS<<1;

	// enable interrupts
	sei();

	startUtrinek();

	uint8_t i = 0;
	for (;;) {
		if (n < 33) {
			for (uint8_t j = 29-n; j > 0; j--) {
				led_toggle_clock();
			}

			SETBIT(*LED_DATA_PORT, LED_DATA_PIN);
			led_toggle_clock();

			if (i>2) {
				CLEARBIT(*LED_DATA_PORT, LED_DATA_PIN);
			}
			led_toggle_clock();

			if (i>0) {
				CLEARBIT(*LED_DATA_PORT, LED_DATA_PIN);
			}
			led_toggle_clock();

			CLEARBIT(*LED_DATA_PORT, LED_DATA_PIN);
			for (uint8_t j = n; j > 0; j--) {
				led_toggle_clock();
			}

			// latch
			led_toggle_latch();

			i++;
			if (i > 32) {
				i = 0;
			}
		}
	}
}

ISR(TWI_SLAVE_vect) {
	static uint8_t state;
	static uint8_t command;
	if (BITSET(TWSSRA, TWASIF)) {
		// received address/stop
		if (BITSET(TWSSRA, TWAS)) {
			// received address
			if (BITSET(TWSSRA, TWDIR)) {
				// read operation
				TWSCRB = 0b00000110;
			} else {
				// write operation
				state = 0;
				TWSCRB = 0b00000011;
			}
		} else {
			// received stop
			TWSCRB = 0b00000010;
		}
	} else if (BITSET(TWSSRA, TWDIF)) {
		// received data
		if (state == 0) {
			command = TWSD;
			state++;
			if (command == 0) {
				startUtrinek();
			}
		}

		TWSCRB = 0b00000011;
	} else {
		TWSCRB = 0b00000111;
	}
}

ISR(TIMER1_COMPA_vect) {
	if (n < 33) {
		OCR1A -= 0x2;
		if (n%3 == 2 and n > 9) {
			OCR0B >>= 1;
		}
		if (n > 10) {
			OCR1A -= 0x4;
			if (n > 20 and OCR1A > 0x1F) {
				OCR1A -= 0xC;
			}
		}
		n++;
	}
}
