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
#include <util/delay.h>

#include <avr/io.h>
#include <avr/interrupt.h>
//#include <avr/pgmspace.h>
#include <avr/eeprom.h>

#include <stdint.h>
#include <stdlib.h>

#include "bitop.h"

static uint8_t ADDRESS;

static uint8_t EEMEM address_eeprom;

static volatile uint16_t random_min;
static volatile uint16_t random_max;
static volatile uint8_t mode;

static uint16_t EEMEM random_min_eeprom;
static uint16_t EEMEM random_max_eeprom;
static uint8_t EEMEM mode_eeprom;

static volatile uint8_t n = 33;

static void startUtrinek() {
	SETBIT(DDRA, PA3);
	OCR0B = BIT(7);
	OCR1A = 0x0100;
	n = 0;
}

static volatile uint16_t random_seconds;
static volatile bool direction;

static void resetRandom() {
	if (mode == 0) { // manual
		OCR2B = 40000;
	} else {
		TCNT0 = 0;
		uint32_t r = random();
		uint16_t random_time = (r & 0x7fff);
		if (random_time > 31249) {
			random_time -= (0x7fff - 31249);
		}
		OCR2B = random_time;
		random_seconds = random_min + ((r >> 15) % (random_max - random_min));
		if (mode == 1) {
			direction = false;
		} else if (mode == 2) {
			direction = true;
		} else {
			direction = bool(r & 1);
		}
	}
}

static const uint8_t LED_ENABLE_PIN = PA7;
static const uint8_t LED_LATCH_PIN = PB2;
static const uint8_t LED_CLOCK_PIN = PB1;
static const uint8_t LED_DATA_PIN = PB0;
static volatile uint8_t * const LED_ENABLE_PORT = &PORTA;
static volatile uint8_t * const LED_LATCH_PORT = &PORTB;
static volatile uint8_t * const LED_CLOCK_PORT = &PORTB;
static volatile uint8_t * const LED_DATA_PORT = &PORTB;
static volatile uint8_t * const LED_ENABLE_DDR = &DDRA;
static volatile uint8_t * const LED_LATCH_DDR = &DDRB;
static volatile uint8_t * const LED_CLOCK_DDR = &DDRB;
static volatile uint8_t * const LED_DATA_DDR = &DDRB;

static inline void led_toggle_clock() {
	SETBIT(*LED_CLOCK_PORT, LED_CLOCK_PIN);
	CLEARBIT(*LED_CLOCK_PORT, LED_CLOCK_PIN);
}

static inline void led_n_clocks(uint8_t n) {
	for (; n > 0; n--) {
		led_toggle_clock();
	}
}

int main() {
	// init
	SETBIT(*LED_ENABLE_DDR, LED_ENABLE_PIN);
	SETBIT(*LED_LATCH_DDR, LED_LATCH_PIN);
	SETBIT(*LED_CLOCK_DDR, LED_CLOCK_PIN);
	SETBIT(*LED_DATA_DDR, LED_DATA_PIN);

	// pull up for active indicator
	SETBIT(PUEA, PA3);

	// pull up for uart receive line
	SETBIT(PUEA, PA2);

	// led test
	// SETBIT(*LED_DATA_PORT, LED_DATA_PIN);
	// for (uint8_t i = 0; i < 33; i++) {
	//     SETBIT(*LED_CLOCK_PORT, LED_CLOCK_PIN);
	//     CLEARBIT(*LED_CLOCK_PORT, LED_CLOCK_PIN);
    //
	//     SETBIT(*LED_LATCH_PORT, LED_LATCH_PIN);
	//     CLEARBIT(*LED_LATCH_PORT, LED_LATCH_PIN);
    //
	//     CLEARBIT(*LED_DATA_PORT, LED_DATA_PIN);
    //
	//     for (uint8_t j = 0; j < 255; j++) {
	//         _delay_ms(1);
	//     }
	// }

	// read eeprom
	ADDRESS = eeprom_read_byte(&address_eeprom);
	if (ADDRESS == 0xff) {
		ADDRESS = 1;
	}
	mode = eeprom_read_byte(&mode_eeprom);
	if (mode == 0xff) {
		mode = 3;
	}
	random_min = eeprom_read_word(&random_min_eeprom);
	if (random_min == 0xffff) {
		random_min = 5;
	}
	random_max = eeprom_read_word(&random_max_eeprom);
	if (random_max == 0xffff) {
		random_max = 60;
	}

	srandom(ADDRESS);

	// timer 1 for advancing led
	OCR1A = 0x0100;
	TCNT1 = 0;
	TCCR1B = 0b00001101;
	SETBIT(TIMSK1, OCIE1A);

	// timer 0 for pwm
	// pwm phase correct, set OC0B on up, clear on down
	TCCR0A = BIT(COM0B1) | BIT(COM0B0) | BIT(WGM00);
	// 15kHz pwm
	TCCR0B = BIT(CS00);
	TOCPMCOE = 0b01000000;

	// timer 2 for random times every second
	// top
	OCR2A = 31250;
	// interrupt value out of range for now
	OCR2B = 40000;
	// 31250Hz timing, CTC mode
	TCCR2B = BIT(CS22) | BIT(WGM22);
	// enable b interrupt
	TIMSK2 = BIT(OCIE2B);

	// enable pin change interrupts
	PCMSK0 = BIT(PCINT3);
	GIMSK = BIT(PCIE0);

	// enable UART
	// enable receive and receive interrupt
	UCSR0B = BIT(RXEN0) | BIT(RXCIE0);
	// 8 bits
	UCSR0C = BIT(UCSZ01) | BIT(UCSZ00);
	// 19200 baud rate
	UBRR0 = 25;

	// enable interrupts
	sei();

	resetRandom();

	uint8_t i = 0;
	for (;;) {
		if (n < 33) {
			if (direction) {
				led_n_clocks(29-n);

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
				led_n_clocks(n);
			} else {
				led_n_clocks(n);

				if (i<=0) {
					SETBIT(*LED_DATA_PORT, LED_DATA_PIN);
				}
				led_toggle_clock();

				if (i<=2) {
					SETBIT(*LED_DATA_PORT, LED_DATA_PIN);
				}
				led_toggle_clock();

				SETBIT(*LED_DATA_PORT, LED_DATA_PIN);
				led_toggle_clock();

				CLEARBIT(*LED_DATA_PORT, LED_DATA_PIN);
				led_n_clocks(29-n);
			}

			// synchronize on pwm
			while (BITCLEAR(TIFR0, TOV0));
			SETBIT(TIFR0, TOV0);

			// latch
			SETBIT(*LED_LATCH_PORT, LED_LATCH_PIN);
			CLEARBIT(*LED_LATCH_PORT, LED_LATCH_PIN);

			i++;
			if (i > 32) {
				i = 0;
			}
		}
	}
}

ISR(PCINT0_vect) {
	if (BITSET(PINA, PA3)) {
		resetRandom();
	} else {
		// disable start utrinek timer while disabled
		OCR2B = 40000;
	}
}

ISR(TIMER2_COMPB_vect) {
	if (random_seconds > 0) {
		random_seconds--;
	} else {
		if (BITSET(PINA, PA3)) {
			startUtrinek();
		}
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
		if (n > 32) {
			// clear active indicator
			CLEARBIT(DDRA, PA3);
		}
	}
}

ISR(USART0_RX_vect) {
	static uint8_t ignore = 0;
	static uint8_t state = 0;
	static uint8_t command = 0;
	static uint16_t word = 0;

	uint8_t data = UDR0;

	if (ignore > 0) {
		ignore--;
		return;
	}

	// delay scheduled utrinek
	random_seconds += 2;

	if (state == 0) {
		if (ADDRESS == (data & 0x0f) || 0 == (data & 0x0f)) {
			state = 1;
		} else {
			ignore = data >> 4;
		}
	} else if (state == 1) {
		command = data;
		if (command == 0) {
			direction = true;
			startUtrinek();
			state = 0;
		} else if (command == 1) {
			direction = false;
			startUtrinek();
			state = 0;
		} else {
			state = 2;
		}
	} else if (state == 2) {
		if (command == 2) {
			mode = data;
			eeprom_update_byte(&mode_eeprom, mode);
			resetRandom();
			state = 0;
		} else if (command == 3 || command == 4) {
			word = data << 8;
			state = 3;
		} else if (command == 5) {
			ADDRESS = data;
			eeprom_update_byte(&address_eeprom, ADDRESS);
			state = 0;
		} else {
			state = 0;
		}
	} else if (state == 3) {
		word |= data;
		if (command == 3) {
			random_min = word;
			eeprom_update_word(&random_min_eeprom, random_min);
		} else if (command == 4) {
			random_max = word;
			eeprom_update_word(&random_max_eeprom, random_max);
		}
		resetRandom();
		state = 0;
	} else {
		state = 0;
	}
}
