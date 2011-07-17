/*  vim:sw=4:ts=4:
**  1-wire protocol library for ATTiny85
**  (C) 2011, Nick Andrew <nick@nick-andrew.net>
**  All Rights Reserved.
*/

#include <avr/io.h>
#include <avr/interrupt.h>

#include <stdint.h>

// I/O pin
#define PIN ( 1 << PORTB0 )

#ifndef CPU_FREQ
#define CPU_FREQ 8000000
#endif

#if CPU_FREQ == 8000000
// Prescaler CK/8
#define PRESCALER ( 1<<CS01 )
// When resetting the devices, use 8us resolution
#define RESET_PRESCALER ( 1<<CS01 | 1<<CS00)
#else
#error "Only CPU_FREQ of 8 MHz is presently supported"
#endif

/*
**  1wire signal timing, in microseconds unless otherwise noted
**
**  GAP_A    Write 1 bit bus low and Read bit bus low
**  GAP_B    Write 1 bit release bus
**  GAP_C    Write 0 bit bus low
**  GAP_D    Write 0 bit release bus
**  GAP_E    Read bit release bus
**  GAP_F    Read bit after sampling
**  GAP_G    Reset devices initial delay (zero so not used)
**  GAP_H    Reset devices bus low (divided by 8)
**  GAP_I    Reset devices release bus
**  GAP_J    Reset devices delay after sampling
*/

#define GAP_A  6
#define GAP_B 64
#define GAP_C 60
#define GAP_D 10
#define GAP_E  9
#define GAP_F 55
#define GAP_G  0
#define GAP_H 60
#define GAP_I  9
#define GAP_J 51

enum onewire0_state {
	OW0_IDLE,
	OW0_WRITE0_LOW,
	OW0_WRITE0_RELEASE,
	OW0_WRITE1_LOW,
	OW0_WRITE1_RELEASE,
	OW0_READ_LOW,
	OW0_READ_SAMPLE,
	OW0_READ_RELEASE,
};

struct onewire {
	volatile uint8_t state;
	volatile uint8_t bit;
};

struct onewire onewire0;

void onewire0_init(void)
{
	onewire0.state = OW0_IDLE;
	// Setup I/O pin, initial tri-state, when enabled output low
	DDRB &= ~( PIN );
	PORTB &= ~( PIN );

	// Setup timer0
	// GTCCR |= ( 1<<TSM );  // Disable the timer for programming
	// GTCCR |= ( 1<<PSR0 );
	// Enable CTC mode
	TCCR0A |= ( 1<<WGM01 );
	TCCR0B &= ~PRESCALER;
	OCR0B = 0xff; // Not used
	TIMSK |= 1<<OCIE0A;
}

static void _starttimer(void)
{
	// Clear any pending timer interrupt
	TIFR |= 1<<OCF0A;
	// Start the timer counting
	TCNT0 = 0;
	TCCR0B |= PRESCALER;
}

static void _release(void)
{
	DDRB &= ~( PIN );
}

static void _pulllow(void)
{
	DDRB |= PIN;
}

static void _writebit(uint8_t value)
{
	TCNT0 = 0;

	if (value) {
		OCR0A = GAP_A;
		onewire0.state = OW0_WRITE1_LOW;
		_pulllow();
	} else {
		OCR0A = GAP_C;
		onewire0.state = OW0_WRITE0_LOW;
		_pulllow();
	}

	while (onewire0.state != OW0_IDLE) { };
}

static uint8_t _readbit(void)
{
	TCNT0 = 0;
	OCR0A = GAP_A;
	onewire0.state = OW0_READ_LOW;
	_pulllow();

	while (onewire0.state != OW0_IDLE) { };

	return onewire0.bit;
}

void onewire0_writebyte(uint8_t byte)
{
	uint8_t bit;

	_starttimer();

	for (bit = 0; bit < 8; bit++) {
		_writebit(byte & 1);
		byte >>= 1;
	}
}

uint8_t onewire0_readbyte(void)
{
	uint8_t bit;
	uint8_t byte = 0;

	_starttimer();

	for (bit = 0; bit < 8; bit++) {
		if (_readbit()) {
			byte |= 0x80;
		}

		byte >>= 1;
	}

	return byte;
}

// Interrupt routine for timer0, OCR0A

ISR(TIMER0_COMPA_vect)
{
	switch(onewire0.state) {
		case OW0_IDLE:
			// Do nothing
			break;

		case OW0_WRITE0_LOW:
			_release();
			OCR0A = GAP_D;
			onewire0.state = OW0_WRITE0_RELEASE;
			break;

		case OW0_WRITE0_RELEASE:
			onewire0.state = OW0_IDLE;
			break;

		case OW0_WRITE1_LOW:
			_release();
			OCR0A = GAP_B;
			onewire0.state = OW0_WRITE1_RELEASE;
			break;

		case OW0_WRITE1_RELEASE:
			onewire0.state = OW0_IDLE;
			break;

		case OW0_READ_LOW:
			_release();
			OCR0A = GAP_E;
			onewire0.state = OW0_READ_SAMPLE;
			break;

		case OW0_READ_SAMPLE:
			onewire0.bit = (PINB & (PIN)) ? 1 : 0;
			OCR0A = GAP_F;
			onewire0.state = OW0_READ_RELEASE;
			break;

		case OW0_READ_RELEASE:
			onewire0.state = OW0_IDLE;
			break;
	}
}
