#include <avr/io.h>
#include <avr/pgmspace.h>
#include <avr/interrupt.h>
#include <util/delay.h>

#define TWI_FAST_MODE 1

#include "USI_TWI_Master.h"

#define PMAX UINT8_MAX
#define PMOD (PMAX+1)

#define P_OFFSET (1*(PMAX+1)/8)
#define P_CENTER (3*(PMAX/8))

#define PCF8583_WRITE_ADDRESS ( 0xA0 & ~(0x01) )
#define PCF8583_READ_ADDRESS  ( PCF8583_WRITE_ADDRESS | 0x01 )
/* used to fetch clock data from PCF8583 IC:
 * [ 10 seconds | 1 seconds ] 0x2
 * [ 10 minutes | 1 minutes ] 0x3
 * [ 10 hours   | 1 hours   ] 0x4
 */
static uint8_t buffer_i2c[5] = {0};

/* unix style timestamp of the current hour, minute and second */
static int16_t epoch = 0;

static void get_time(void) {
	buffer_i2c[0] = PCF8583_WRITE_ADDRESS;
	buffer_i2c[1] = 0x02; // start of time data
	USI_TWI_Start_Transceiver_With_Data(buffer_i2c, 2);
	buffer_i2c[0] = PCF8583_READ_ADDRESS;
	USI_TWI_Start_Transceiver_With_Data(buffer_i2c, 4);
	/* calculate Unix style timestamp (but only time, not date) */
	epoch = (buffer_i2c[1] & 0x0F) + (buffer_i2c[1] >> 4)*10 +
	        ((buffer_i2c[2] & 0x0F) + (buffer_i2c[2] >> 4)*10)*60 +
	        ((buffer_i2c[3] & 0x0F) + (buffer_i2c[3] >> 4)*10)*60*60;
}

typedef uint8_t(*display_t)(uint8_t);

static volatile uint8_t ANIMATION_PHASE = 0;

static uint8_t display_clock(uint8_t pos) {
	uint8_t seconds = (epoch % 60);
	uint8_t minutes = (epoch % (60*60))/60;
	uint8_t hours = (epoch % (uint16_t)(60*60*60))/(uint16_t)(60*60) % 24;
	/* scale everything to 192 units */
	uint8_t sec_hand = (((uint16_t)(3*PMOD/4)*seconds) / 60);
	uint8_t min_hand = (((uint16_t)(3*PMOD/4)*minutes) / 60);
	uint8_t hr_hand = (((uint16_t)(3*PMOD/4)*(hours%12)) / 12);

	return ( (hours < 12) ? pos < hr_hand : pos > hr_hand )
	       ^ (abs(min_hand-pos) < 3)
	       ^ (abs(sec_hand-pos) < 3)
	;
}

static uint8_t display_magic_eye(uint8_t pos) {
	uint8_t width = ANIMATION_PHASE;
	uint8_t left = (uint16_t)P_CENTER+(width/2)%PMOD;
	uint8_t right = (uint16_t)P_CENTER+PMOD-(width/2)%PMOD;
	if (pos < left && pos > right) {
		return 1;
	} else {
		return 0;
	}
}

static uint8_t display_radar(uint8_t pos) {
	static int8_t dir = 1;
	uint8_t width = 40;
	uint8_t start = 0;
	uint8_t end = 0;
	if (dir == 1) {
		start = ANIMATION_PHASE;
		if ((start+width) > 128+64) {
			ANIMATION_PHASE = 1;
			dir = -1;
		}
	} else if (dir == -1) {
		start = (128+64)-ANIMATION_PHASE-width;
		if (start > 128+64) {
			ANIMATION_PHASE = 1;
			dir = 1;
		}
	}
	end = ((uint16_t)start+width)%PMOD;
	if (pos > start && pos < end) {
		return 1;
	} else {
		return 0;
	}
}

static uint8_t display_progress(uint8_t pos) {
	uint8_t progress = ANIMATION_PHASE;
	if (pos < progress) {
		return 1;
	} else {
		return 0;
	}
}

static uint8_t display_half(uint8_t pos) {
	return pos < PMAX/2;
}

static uint8_t image[(PMOD+7)/8] = { 0xFF };

static uint8_t display_precalc(uint8_t pos) {
	uint8_t f = image[pos/8];
	return f & 1<<(pos%8);
}

static void precalc_image(display_t d) {
	uint8_t i = 0;
	for (i=0; i<PMOD; i++) {
		if (1||d(i)) {
			image[i/8] |= 1<<(i%8);
		} else {
			image[i/8] &= ~(1<<(i%8));
		}
	}
}

#define N_DISPLAYS 6
static display_t display[N_DISPLAYS] = {
	&display_progress,
	&display_radar,
	&display_magic_eye,
	&display_half,
	&display_clock,
	&display_precalc,
};

static uint16_t getCounter(void) {
	return TCNT1;
}

static uint16_t resetCounter(void) {
	uint16_t old = getCounter();
	TCNT1 = 0;
	return old;
}

/* memorize the duration of one rotation in clock ticks */
static volatile uint16_t duration = 0;

int main(void) {
	DDRB = (1<<PB3);

	// turn on pull up
	PORTB = (1<<PB4);

	DDRD = (1<<PD5);

	TCCR0A = (1<<WGM01);
	TCCR0B = (1<<CS02 | 1<<CS00);
	OCR0A = 255;

	TCCR1A = (1<<WGM12);
	TCCR1B = (1<<CS12 | 0<<CS11 | 0<<CS10);

	TIMSK = (1<<OCIE0A);

	GIMSK = (1<<PCIE);
	PCMSK = (1<<PCINT4);

	USI_TWI_Master_Initialise();

	uint8_t pos = 0;

	/* which animation will be shown? */
	uint8_t display_i = 4;
	/* we fetch time data from the RTC chip
	 * whenever we enter the covered area
	 */
	static uint8_t fetched_time = 0;

	// test LEDs
	PORTB &= ~(1<<PB3);
	_delay_ms(500);
	PORTB |= (1<<PB3);
	sei();

	while (1) {
		if (!duration) continue;

		// where are we exactly?
		pos = PMOD-( ((((((uint32_t)getCounter())<<16) / duration)*PMAX)>>16)+P_OFFSET ) % (PMOD);
		// are we inside the covered section?
		if (pos > 3*(PMOD/4)) {
			PORTB |= 1<<PB3;
			if (!fetched_time) {
				fetched_time = 1;
				get_time();
			}
		} else {
			fetched_time = 0;
			if (display[display_i](pos)) {
				PORTB &= ~(1<<PB3);
			} else {
				PORTB |= 1<<PB3;
			}
		}
	}
}

ISR(TIMER0_COMPA_vect) {
	if (ANIMATION_PHASE < 255) {
		ANIMATION_PHASE++;
	} else {
		ANIMATION_PHASE = 0;
	}
}

ISR(PCINT_vect) {
	uint16_t t = getCounter();
	/* check for rising edge and debounce */
	if (PINB & 1<<PB4) {
		duration = resetCounter();
	}
}
