#include <stdlib.h>
#include <avr/io.h>
#include <avr/pgmspace.h>
#include <avr/interrupt.h>
#include <util/delay.h>


#define DO_PRECALC 0

#define TWI_FAST_MODE 1

#include "USI_TWI_Master.h"

#define PMAX UINT8_MAX
#define PMOD (PMAX+1)

#define P_VISIBLE ((PMOD*3)/4)

#define P_OFFSET (1*(PMAX+1)/8)
#define P_CENTER (3*(PMAX/8))

#define PCF8583_WRITE_ADDRESS ( 0xA0 & ~(0x01) )
#define PCF8583_READ_ADDRESS  ( PCF8583_WRITE_ADDRESS | 0x01 )
/* used to fetch clock data from I2C
 * PCF8583 memory layout:
 * [ 1/10 seoncds | 1/100 seconds ] 0x1
 * [   10 seconds |     1 seconds ] 0x2
 * [   10 minutes |     1 minutes ] 0x3
 * [   10 hours   |     1 hours   ] 0x4
 */
static uint8_t buffer_i2c[6] = {0};

static struct {
	uint8_t h;
	uint8_t m;
	uint8_t s;
	uint8_t cs;
} clock = {0,0,0};

/* get a unix style timestamp value from the clock */
static int32_t get_timestamp(void) {
	int32_t result = 0;
	result += clock.s;
	result += clock.m*60;
	result += clock.h*60*60;
	return result;
}

static void blink(uint8_t n) {
	while (n--) {
		PORTD |= 1<<PD5;
		_delay_ms(200);
		PORTD &= ~(1<<PD5);
		_delay_ms(200);
	}
}

static void update_clock(void) {
	buffer_i2c[0] = PCF8583_WRITE_ADDRESS;
	buffer_i2c[1] = 0x01; // start of time data
	USI_TWI_Start_Transceiver_With_Data(buffer_i2c, 2);
	buffer_i2c[0] = PCF8583_READ_ADDRESS;
	USI_TWI_Start_Transceiver_With_Data(buffer_i2c, 5);
	clock.cs = (buffer_i2c[1] & 0x0F) + (buffer_i2c[1] >> 4)*10;
	clock.s = (buffer_i2c[2] & 0x0F) + (buffer_i2c[2] >> 4)*10;
	clock.m = ((buffer_i2c[3] & 0x0F) + (buffer_i2c[3] >> 4)*10);
	clock.h = ((buffer_i2c[4] & 0x0F) + (buffer_i2c[4] >> 4)*10);
}

static void set_timestamp(int32_t ts) {
	uint8_t seconds = (ts % 60);
	uint8_t minutes = (ts % (60*60))/60;
	uint8_t hours = (ts / (60*60));
	buffer_i2c[0] = PCF8583_WRITE_ADDRESS;
	buffer_i2c[1] = 0x02; // start of time data
	buffer_i2c[2] = ( ((seconds/10)<<4) | (seconds%10) );
	buffer_i2c[3] = ( ((minutes/10)<<4) | (minutes%10) );
	buffer_i2c[4] = ( ((hours/10)<<4) | (hours%10) );
	USI_TWI_Start_Transceiver_With_Data(buffer_i2c, 5);
}

static void set_clock(int8_t h, int8_t m, int8_t s) {
	set_timestamp( (int32_t)h*60*60 + (int32_t)m*60 + s );
}

typedef uint8_t(*display_t)(uint8_t);

static volatile uint8_t ANIMATION_PHASE = 0;

static uint8_t display_clock(uint8_t pos) {
	static int32_t ts = 0;
	static uint8_t cs = 0;
	int32_t time = get_timestamp();
	/* scale everything to the visible number of units */
	static uint8_t hs_hand = 0;
	static uint8_t cs_hand = 0;
	static uint8_t s_hand = 0;
	static uint8_t m_hand = 0;
	static uint8_t h_hand = 0;
	if (0 || ts != time || cs != clock.cs) {
		cs_hand = (((int16_t)(P_VISIBLE)*clock.cs) / 100);
		s_hand = (((int16_t)(P_VISIBLE)*clock.s) / 60);
		m_hand = (((int16_t)(P_VISIBLE)*clock.m) / 60);
		h_hand = (((int16_t)(P_VISIBLE)*(clock.h%12)) / 12);
		ts = time;
		cs = clock.cs;
	}

	return ( (clock.h < 12) ? pos < h_hand : pos > h_hand )
	       ^ (abs(m_hand-pos) < 4)
	       ^ (abs(s_hand-pos) < 2)
	       ^ (abs(cs_hand-pos) < 1)
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

static uint8_t display_every_other(uint8_t pos) {
	return pos%2;
}

static uint8_t image[(PMOD+7)/8] = { 0 };

static uint8_t display_precalc(uint8_t pos) {
	if (pos >= PMOD) return 0;
	uint8_t f = image[pos/8];
	return f & 1<<(pos%8);
}

static void precalc_image(display_t d) {
	uint8_t i = 0;
	for (i=0; i<PMOD; i++) {
		if (d(i)) {
			image[i/8] |= 1<<(i%8);
		} else {
			image[i/8] &= ~(1<<(i%8));
		}
	}
}

#define N_DISPLAYS 7
static display_t display[N_DISPLAYS] = {
	&display_progress,
	&display_radar,
	&display_magic_eye,
	&display_half,
	&display_clock,
	&display_precalc,
	&display_every_other,
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
#define N_DURATIONS 1
static volatile uint16_t duration[N_DURATIONS] = {0};
static volatile uint16_t avg_duration = 0;

static uint16_t get_duration(void) {
	uint32_t s = 0;
	uint8_t i = 0;
	for (i=0; i<N_DURATIONS; i++) {
		s += duration[i];
	}
	return s/N_DURATIONS;
}

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

	int16_t duration = 0;
	while (1) {
		duration = avg_duration;
		if (!duration) continue;

		// where are we exactly?
		pos = PMOD-( ((((((uint32_t)getCounter())<<16) / duration)*PMAX)>>16)+P_OFFSET ) % (PMOD);
		// are we inside the covered section?
		if (pos >= P_VISIBLE) {
			PORTB |= 1<<PB3;
			if (!fetched_time) {
				fetched_time = 1;
				update_clock();
#if DO_PRECALC
				precalc_image(&display[display_i]);
#endif
			}
		} else {
			fetched_time = 0;
#if DO_PRECALC
			if (display_precalc(pos)) {
#else
			if (display[display_i](pos)) {
#endif
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
	static uint8_t d_i = 0;
	/* check for rising edge and debounce */
	if (PINB & 1<<PB4) {
		duration[d_i] = resetCounter();
		d_i++;
		d_i %= N_DURATIONS;
		avg_duration = get_duration();
	}
}
