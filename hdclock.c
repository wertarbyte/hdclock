#include <avr/io.h>
#include <avr/pgmspace.h>
#include <avr/interrupt.h>
#include <util/delay.h>

#define PMAX UINT8_MAX
#define PMOD (PMAX+1)

#define P_OFFSET (1*(PMAX+1)/8)
#define P_CENTER (3*(PMAX/8))

static volatile uint8_t OVERFLOW = 0;

static uint8_t display_magic_eye(uint8_t pos) {
	uint8_t width = 10;
	uint8_t left = (uint16_t)P_CENTER+(width/2)%PMOD;
	uint8_t right = (uint16_t)P_CENTER+PMOD-(width/2)%PMOD;
	if (pos < left && pos > right) {
		return 1;
	} else {
		return 0;
	}
}

static uint8_t display_radar(uint8_t pos) {
	uint8_t width = 10;
	uint8_t start = 0;
	uint8_t end = ((uint16_t)start+width)%PMOD;
	if (pos > start && pos < end) {
		return 1;
	} else {
		return 0;
	}
}

static uint8_t display_progress(uint8_t pos) {
	uint8_t progress = 0;
	if (pos < progress) {
		return 1;
	} else {
		return 0;
	}
}

static uint8_t display_half(uint8_t pos) {
	return pos < PMAX/2;
}

static uint16_t getCounter(void) {
	uint16_t result = (((uint16_t)OVERFLOW)<<8) | TCNT0;
	return result;
}

static uint16_t resetCounter(void) {
	uint16_t old = getCounter();
	OVERFLOW = 0;
	TCNT0 = 0;
	return old;
}

/* memorize the duration of one rotation in clock ticks */
static volatile uint16_t duration = 0;

int main(void) {
	DDRB |= (1<<PB3);

	// turn on pull up
	PORTB |= (1<<PB4);

	TCCR0B |= (1<<CS02 | 0<<CS01 | 0<<CS00);
	TIMSK |= (1<<TOIE0);
	GIMSK |= (1 <<PCIE);
	PCMSK |= (1<<PCINT4);

	uint8_t pos = 0;

	uint8_t (*display)(uint8_t) = 0;
	display = &display_half;

	// test LEDs
	PORTB &= ~(1<<PB3);
	_delay_ms(500);
	PORTB |= (1<<PB3);
	sei();
	while (1) {
		if (!duration) continue;
		if (!display) continue;

		// where are we exactly?
		pos = PMOD-( ((((((uint32_t)getCounter())<<16) / duration)*PMAX)>>16)+P_OFFSET ) % (PMOD);
		if ((*display)(pos)) {
			PORTB &= ~(1<<PB3);
		} else {
			PORTB |= 1<<PB3;
		}
	}
}

ISR(TIM0_OVF_vect) {
	OVERFLOW++;
}

ISR(PCINT0_vect) {
	uint16_t t = getCounter();
	/* check for rising edge and debounce */
	if (PINB & 1<<PB4 && t > 64) {
		duration = resetCounter();
	}
}
