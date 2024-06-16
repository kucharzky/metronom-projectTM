#include <avr/io.h>
#include <util/delay.h>
#include <avr/interrupt.h>
#include <stdbool.h>

#define SEGMENT_PORT PORTD
#define SEGMENT_DDR DDRD

#define ANODE_PORT PORTB
#define ANODE_DDR DDRB

#define MAX_TEMPO 300      // Maksymalna wartoœæ tempa
#define MIN_TEMPO 30       // Minimalna wartoœæ tempa
#define TEMPO_RESET 100     // Wartoœæ pocz¹tkowa tempa

#define MAX_DUTY_CYCLE 255
#define MIN_DUTY_CYCLE 0

// Definicje segmentów wyœwietlacza 0-9
const uint8_t segment_digits[] = {
	~0x7E, ~0x30, ~0x6D, ~0x79, ~0x33, ~0x5B, ~0x5F, ~0x70, ~0x7F, ~0x7B
};

// Wejœcia/wyjœcia
void init_io(void) {
	SEGMENT_DDR = 0xFF;
	ANODE_DDR = 0x0F;

	// Ustawienie pinu PB7 jako wejœcia dla SW0
	DDRB &= ~(1 << DDB7);
	// W³¹czenie rezystora pull-up na PB7
	PORTB |= (1 << PORTB7);

	// Ustawienie pinów PC0-PC3 jako wejœcia z w³¹czonymi rezystorami pull-up
	DDRC &= ~(1 << DDC0) & ~(1 << DDC1) & ~(1 << DDC2) & ~(1 << DDC3);
	PORTC |= (1 << PORTC0) | (1 << PORTC1) | (1 << PORTC2) | (1 << PORTC3);

	// Ustawienie pinów PC4 i PC5 jako wejœcia z w³¹czonymi rezystorami pull-up
	DDRC &= ~(1 << DDC4) & ~(1 << DDC5);
	PORTC |= (1 << PORTC4) | (1 << PORTC5);

	// Konfiguracja pin change interrupt dla portu B
	PCICR |= (1 << PCIE0);     // W³¹czenie przerwañ pin change dla portu B
	PCMSK0 |= (1 << PCINT7);   // W³¹czenie przerwania pin change dla pinu PB7
}

// Wyœwietlanie liczby na danej pozycji
void display_digit(uint8_t digit, uint8_t position) {
	// Zerowanie przy starcie
	SEGMENT_PORT = 0xFF;

	// Ustaw segmenty dla danej liczby
	if (digit <= 9) {
		SEGMENT_PORT = segment_digits[digit];
	}

	// Miejsce liczby na wyœwietlaczu
	ANODE_PORT = ~(1 << position);

	_delay_ms(5);

	// Wy³¹cz po wyœwietleniu
	ANODE_PORT = 0xFF;
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

volatile bool buzzer_on = false;
volatile bool metronome_active = false;
volatile uint16_t tempo = TEMPO_RESET;
volatile uint8_t duty_cycle = 255; // Pocz¹tkowe wype³nienie PWM 100%

void pwm_init(void)
{
	// Ustawienie pinu PC4 jako wyjœcia
	DDRE |= (1 << DDE0);

	// Konfiguracja Timer1 dla generowania przerwañ PWM
	TCCR1A |= (1 << WGM11); // Fast PWM, ICR1 jako TOP
	TCCR1B |= (1 << WGM13) | (1 << WGM12) | (1 << CS10); // Fast PWM, no prescaling
	ICR1 = 255; // Ustawienie 255 na wartoœæ okreœlaj¹c¹ czêstotliwoœæ PWM
	OCR1A = duty_cycle; // Pocz¹tkowe wype³nienie PWM

	// W³¹czenie przerwañ dla Timer1
	TIMSK1 |= (1 << OCIE1A) | (1 << TOIE1);
}

void set_pwm_duty_cycle(uint8_t duty_cycle)
{
	OCR1A = duty_cycle; // Ustawienie wype³nienia PWM (0-255)
}

// Przerwanie porównawcze dla Timer1 (dla OCR1A)
ISR(TIMER1_COMPA_vect)
{
	if (buzzer_on)
	{
		PORTE |= (1 << PORTE0); // Ustawienie PC4 na wysoki stan
	}
}

// Przerwanie przepe³nienia dla Timer1 (dla ICR1)
ISR(TIMER1_OVF_vect)
{
	PORTE &= ~(1 << PORTE0); // Ustawienie PC4 na niski stan
}

void metronome_init(void)
{
	// W³¹czenie globalnych przerwañ
	sei();
}

void toggle_buzzer(void)
{
	buzzer_on = !buzzer_on;
	if (buzzer_on)
	{
		set_pwm_duty_cycle(duty_cycle); // Ustawienie wype³nienia PWM na bie¿¹c¹ wartoœæ
	}
	else
	{
		set_pwm_duty_cycle(0); // Wy³¹czenie PWM
	}
}

bool debounce(uint8_t pinA, uint8_t pinB)
{
	if (!(pinA & (1 << pinB)))
	{
		_delay_ms(50); // D³u¿sze opóŸnienie debounce
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

	if (!(PINC &= ~(1 << PINC4))) // PC4: Pierwszy rz¹d - zmiana tempa
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
	else if (!(PINC &= ~(1 << PINC5))) // PC5: Drugi rz¹d - zmiana duty cycle
	{
		if (current_state_C0 && !prev_state_C0)
		{
			if (duty_cycle <= MAX_DUTY_CYCLE - 10) {
				duty_cycle += 10;
				} else {
				duty_cycle = MAX_DUTY_CYCLE;
			}
		}
		else if (current_state_C1 && !prev_state_C1)
		{
			if (duty_cycle <= MAX_DUTY_CYCLE - 5) {
				duty_cycle += 5;
				} else {
				duty_cycle = MAX_DUTY_CYCLE;
			}
		}
		else if (current_state_C2 && !prev_state_C2)
		{
			if (duty_cycle >= MIN_DUTY_CYCLE + 5) {
				duty_cycle -= 5;
				} else {
				duty_cycle = MIN_DUTY_CYCLE;
			}
		}
		else if (current_state_C3 && !prev_state_C3)
		{
			if (duty_cycle >= MIN_DUTY_CYCLE + 10) {
				duty_cycle -= 10;
				} else {
				duty_cycle = MIN_DUTY_CYCLE;
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
	pwm_init();
	metronome_init();
	init_io();

	uint32_t interval = 700000 / tempo; // Pocz¹tkowy interwa³

	while (1)
	{
		if (PINC != 0x0F)
		{
			keypad();
			interval = 700000 / tempo; // Przelicz interwa³ po ka¿dej zmianie tempa
			display_number(tempo);
		}

		if (metronome_active)
		{
			toggle_buzzer(); // Prze³¹cz buzzer
			delay_ms(interval / 2); // Poczekaj po³owê interwa³u
			toggle_buzzer(); // Wy³¹cz buzzer
			delay_ms(interval / 2); // Poczekaj po³owê interwa³u
		}

		delay_ms(5); // Krótkie opóŸnienie, aby zredukowaæ efekt drgania styków przycisku
	}

	return 0;
}
