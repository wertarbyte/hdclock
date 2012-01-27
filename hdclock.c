#include <avr/io.h>
#include <avr/pgmspace.h>
#include <avr/interrupt.h>
#include <util/delay.h>

#define CPU_PRESCALE(n) (CLKPR = 0x80, CLKPR = (n))
#define CPU_16MHz       0x00
#define CPU_8MHz        0x01
#define CPU_4MHz        0x02
#define CPU_2MHz        0x03
#define CPU_1MHz        0x04
#define CPU_500kHz      0x05
#define CPU_250kHz      0x06
#define CPU_125kHz      0x07
#define CPU_62kHz       0x08

#define PMAX UINT8_MAX
#define PMOD (PMAX+1)

#define P_OFFSET (1*(PMAX+1)/8)
#define P_CENTER (3*(PMAX/8))

static uint8_t read_poti(void) {
	ADCSRA |= 1<<ADSC;
	while (ADCSRA & 1<<ADSC) {}
	return ADCH;
}

static uint8_t display_magic_eye(uint8_t pos) {
	uint8_t width = read_poti();
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
	uint8_t start = read_poti();
	uint8_t end = ((uint16_t)start+width)%PMOD;
	if (pos > start && pos < end) {
		return 1;
	} else {
		return 0;
	}
}

static uint8_t display_progress(uint8_t pos) {
	if (pos < read_poti()) {
		return 1;
	} else {
		return 0;
	}
}

int main(void) {
	CPU_PRESCALE(CPU_16MHz);
	DDRD |= (1<<PD6 | 1<<PD0);

	ADMUX = (0<<REFS1 | 1<<REFS0 | 1<<ADLAR);
	ADCSRA |= 1<<ADEN;
	
	TCCR1B |= (0<<CS12 | 1<<CS11 | 0<<CS10);

	uint8_t old_state = 0;
	uint8_t sensor_state = 0;
	uint16_t duration = 0;


	uint8_t pos = 0;

	uint8_t (*display)(uint8_t) = 0;
	display = &display_progress;

	while (1) {
		sensor_state = !(PIND & 1<<PD1);
		if (sensor_state && !old_state) {
			duration = TCNT1;
			TCNT1 = 0;
		}
		old_state = sensor_state;
		if (sensor_state) {
			PORTD &= ~(1<<PD6);
		} else {
			PORTD |= 1<<PD6;
		}

		if (!duration) continue;
		if (!display) continue;

		// where are we exactly?
		pos = PMOD-( ((((((uint32_t)TCNT1)<<16) / duration)*PMAX)>>16)+P_OFFSET ) % (PMOD);
		if ((*display)(pos)) {
			PORTD &= ~(1<<PD0);
		} else {
			PORTD |= 1<<PD0;
		}
	}
}
