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

static volatile uint8_t n = 0;
static const uint8_t I2C_ADDRESS = 0x40;

static void startUtrinek() {
	OCR0B = BIT(7);
	OCR1A = 0x0100;
	n = 0;
}

int main() {
	// init
	SETBIT(DDRA, PA7);
	
	SETBIT(DDRA, PA1);
	SETBIT(DDRA, PA2);
	SETBIT(DDRA, PA3);
	
	// timer 1 for advancing led
	TCCR1B = 0b00001101;
    OCR1A = 0x0200;
	SETBIT(TIMSK1, OCIE1A);
	
	// timer 0 for pwm
	TCCR0A = 0b00110001;
	TCCR0B = 0b010;
	
	// usi
	SETBIT(PORTA, PA6);
	SETBIT(PORTA, PA4);
	CLEARBIT(DDRA, PA6);
	CLEARBIT(DDRA, PA4);
	USICR = (1<<USISIE)|(1<<USIOIE)|         // Enable Start Condition Interrupt. Disable Overflow Interrupt.
		(1<<USIWM1)|(0<<USIWM0)|             // Set USI in Two-wire mode. No USI Counter overflow prior
                                             // to first Start Condition (potentail failure)
        (1<<USICS1)|(0<<USICS0)|(0<<USICLK)| // Shift Register Clock Source = External, positive edge
        (0<<USITC);	
	USISR = 0xF0; // Clear all flags and reset overflow counter
    
    n = 50;
	//startUtrinek();

	// enable interrupts
	sei();

	uint8_t i = 0;
	for (;;) {
		for (uint8_t j = 29-n; j > 0; j--) {
			SETBIT(PORTA, PA2);
			CLEARBIT(PORTA, PA2);
		}
		
		SETBIT(PORTA, PA3);
		SETBIT(PORTA, PA2);
		CLEARBIT(PORTA, PA2);
		
		if (i>2) {
			CLEARBIT(PORTA, PA3);
		}
		SETBIT(PORTA, PA2);
		CLEARBIT(PORTA, PA2);
		
		if (i>0) {
			CLEARBIT(PORTA, PA3);
		}
		SETBIT(PORTA, PA2);
		CLEARBIT(PORTA, PA2);
		
		CLEARBIT(PORTA, PA3);
		for (uint8_t j = n; j > 0; j--) {
			SETBIT(PORTA, PA2);
			CLEARBIT(PORTA, PA2);
		}

		// latch
		SETBIT(PORTA, PA1);
		CLEARBIT(PORTA, PA1);

		i++;
		if (i > 32) {
			i = 0;
		}
	}
}

ISR(TIM1_COMPA_vect) {
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

// i2c implementation
volatile uint8_t i2c_state = 0xff;

static inline void sendAck() {
	USIDR    =  0;                                              /* Prepare ACK                         */ 
	SETBIT(DDRA, PA6);                              /* Set SDA as output                   */ 
	USISR    =  (0<<USISIF)|(1<<USIOIF)|(1<<USIPF)|(1<<USIDC)|  /* Clear all flags, except Start Cond  */ 
				(0x0F<<USICNT0);                                /* set USI counter to shift 1 bit. */ 
}

static inline void readData() {
	CLEARBIT(DDRA,PA6);                       /* Set SDA as intput */                   
	USISR    =  (0<<USISIF)|(1<<USIOIF)|(1<<USIPF)|(1<<USIDC)|      /* Clear all flags, except Start Cond */ \
                (0x0<<USICNT0);                                     /* set USI to shift out 8 bits        */ \
}

ISR(USI_OVF_vect) {
	static uint8_t dataCount = 0;
	
	switch (i2c_state) {
		case 0:
			if (( USIDR>>1 ) == I2C_ADDRESS) {
				if (BITCLEAR(USIDR, 0)) {
					dataCount = 0;
					i2c_state = 1;
					sendAck();
				} else {
					i2c_state = 0xff;
					USISR  =    (1<<USISIF)|(1<<USIOIF)|(1<<USIPF)|(1<<USIDC)|      // Clear flags
                (0x0<<USICNT0);                                     // Set USI to sample 8 bits i.e. count 16 external pin toggles.
				}
			} else {
				i2c_state = 0xff;
				USISR  =    (1<<USISIF)|(1<<USIOIF)|(1<<USIPF)|(1<<USIDC)|      // Clear flags
                (0x0<<USICNT0);                                     // Set USI to sample 8 bits i.e. count 16 external pin toggles.
			}
			break;
		case 1:
			i2c_state = 2;
			readData();
			break;
		case 2:
			if (dataCount == 0) {
				switch (USIDR) {
					case 0:
						startUtrinek();
						break;
				}
				dataCount++;
			}
			i2c_state = 1;
			sendAck();
			break;
		default:
			USISR  =    (1<<USISIF)|(1<<USIOIF)|(1<<USIPF)|(1<<USIDC)|      // Clear flags
               (0x0<<USICNT0);                                     // Set USI to sample 8 bits i.e. count 16 external pin toggles.
			break;
 
	}
}

ISR(USI_STR_vect) {
	i2c_state = 0;
	USISR  =    (1<<USISIF)|(1<<USIOIF)|(1<<USIPF)|(1<<USIDC)|      // Clear flags
                (0x0<<USICNT0);                                     // Set USI to sample 8 bits i.e. count 16 external pin toggles.

}
