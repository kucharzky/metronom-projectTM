#define F_CPU 2000000UL
#include <avr/io.h>
#include <util/delay.h>
#include <avr/interrupt.h>
#include <stdbool.h>

#define MAX_TEMPO 300      
#define MIN_TEMPO 30       
#define TEMPO_RESET 100

volatile uint8_t duty_cycle = 255;
volatile bool buzzer_on = false;
volatile bool metronome_active = false;
volatile uint16_t tempo = TEMPO_RESET;


const uint8_t segment_digits[] = {
	~0x7E, ~0x30, ~0x6D, ~0x79, ~0x33, ~0x5B, ~0x5F, ~0x70, ~0x7F, ~0x7B
};

void io_pwm_init(void) {
// i/o
	DDRD = 0xFF;
	DDRB = 0x0F;

	DDRB &= ~(1 << DDB7);
	PORTB |= (1 << PORTB7);

	DDRC &= ~(1 << DDC0) & ~(1 << DDC1) & ~(1 << DDC2) & ~(1 << DDC3);
	PORTC |= (1 << PORTC0) | (1 << PORTC1) | (1 << PORTC2) | (1 << PORTC3);

// przerwania
	PCICR |= (1 << PCIE0);
	PCMSK0 |= (1 << PCINT7);
// pwm

	DDRE |= (1 << DDE0);

	TCCR1A |= (1 << WGM11);
	TCCR1B |= (1 << WGM13) | (1 << WGM12) | (1 << CS10);
	ICR1 = 255;
	OCR1A = duty_cycle;

	TIMSK1 |= (1 << OCIE1A) | (1 << TOIE1);

}

void display_digit(uint8_t digit, uint8_t position) {
	PORTD = 0xFF;

	if (digit <= 9) {
		PORTD = segment_digits[digit];
	}
	PORTB = ~(1 << position);

	_delay_ms(5);
	PORTB = 0xFF;
}

void display_number(uint16_t number) {
	display_digit((number / 1000) % 10, PB3);
	display_digit((number / 100) % 10, PB2);
	display_digit((number / 10) % 10, PB1);
	display_digit(number % 10, PB0);
}

void delay_ms(uint16_t ms)
{
	for (uint16_t i = 0; i < ms; i++)
	{
		_delay_ms(1);
	}
}

void set_pwm_duty_cycle(uint8_t duty_cycle)
{
	OCR1A = duty_cycle;
}

ISR(TIMER1_COMPA_vect)
{
	if (buzzer_on)
	{
		PORTE |= (1 << PORTE0);
	}
}

ISR(TIMER1_OVF_vect)
{
	PORTE &= ~(1 << PORTE0); // Ustawienie PC4 na niski stan
}

void metronome_init(void)
{
	sei();
}

void toggle_buzzer(void)
{
	buzzer_on = !buzzer_on;
	if (buzzer_on)
	{
		set_pwm_duty_cycle(duty_cycle);
	}
	else
	{
		set_pwm_duty_cycle(0);
	}
}

bool debounce(uint8_t pinA, uint8_t pinB)
{
	if (!(pinA & (1 << pinB)))
	{
		_delay_ms(50);
		if (!(pinA & (1 << pinB)))
		{
			return true;
		}
	}
	return false;
}

void keypad()
{
	static bool prev_state_C0 = true;
	static bool prev_state_C1 = true;
	static bool prev_state_C2 = true;
	static bool prev_state_C3 = true;

	bool current_state_C0 = !(PINC & (1 << PINC0));
	bool current_state_C1 = !(PINC & (1 << PINC1));
	bool current_state_C2 = !(PINC & (1 << PINC2));
	bool current_state_C3 = !(PINC & (1 << PINC3));

	{
		if (current_state_C0 && !prev_state_C0)
		{
			tempo -= 5;
			if (tempo < MIN_TEMPO) {
				tempo = MAX_TEMPO;
			}
		}
		else if (current_state_C1 && !prev_state_C1)
		{
			tempo -= 1;
			if (tempo < MIN_TEMPO) {
				tempo = MAX_TEMPO;
			}
		}
		else if (current_state_C2 && !prev_state_C2)
		{
			tempo += 1;
			if (tempo > MAX_TEMPO) {
				tempo = MIN_TEMPO;
			}
		}
		else if (current_state_C3 && !prev_state_C3)
		{
			tempo += 5;
			if (tempo > MAX_TEMPO) {
				tempo = MIN_TEMPO;
			}
		}
	}
	prev_state_C0 = current_state_C0;
	prev_state_C1 = current_state_C1;
	prev_state_C2 = current_state_C2;
	prev_state_C3 = current_state_C3;
}

ISR(PCINT0_vect)
{
	static bool prev_button_state = true;
	bool button_pressed = debounce(PINB, PINB7);

	if (button_pressed && !prev_button_state)
	{
		metronome_active = !metronome_active;
	}

	prev_button_state = button_pressed;
}

int main(void)
{
	io_pwm_init();
	metronome_init();
	uint32_t interval = 350000 / tempo;

	while (1)
	{
		if (PINC != 0x0F)
		{
			keypad();
			interval = 350000 / tempo;
			display_number(tempo);
		}
		if (metronome_active)
		{
			toggle_buzzer();
			delay_ms(interval / 2);
			toggle_buzzer();
			delay_ms(interval / 2);
		}
		delay_ms(50);
	}

	return 0;
}
