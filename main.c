#define F_CPU 2000000UL
#include <avr/io.h>
#include <util/delay.h>
#include <avr/interrupt.h>

volatile uint16_t t1_done = 1;
void delay_t1(uint16_t ms){
	t1_done = 0;
	OCR1A = 31.25*ms; // 2MHZ / 64 * 100 = 31.25
	TIMSK1 |= (1 << OCR1A);
	sei();
}

// DEFINICJE PORTÓW I REJESETRY
#define SEMGENT7_PORT PORTD
#define SEGMENT7_DDR DDRD
#define D_SEGMENT7_PORT PORTB
#define D_SEGMENT7_DDR DDRB
#define KEYPAD_PORT PORTC
#define KEYPAD_DDR DDRC
#define BUZZER_PORT PORTE
#define BUZZER_DDR DDRE

// STEROWANIE TEMPEM (BPM)
#define MAX_TEMPO 300      
#define MIN_TEMPO 30       
#define TEMPO_RESET 100
volatile uint16_t tempo = TEMPO_RESET;
// USTAWIENIE WYPE£NIENIA PWM (sterowanie g³oœnoœci¹)
volatile uint8_t duty_cycle = 255;

// FLAGI
volatile uint8_t buzzer_on = 0;
volatile uint8_t metronome_active = 0;

// ZDEFINIOWANIE LICZB NA WYŒWIETLACZU 7SEG
const uint8_t segment_digits[] = {
	~0x7E, ~0x30, ~0x6D, ~0x79, ~0x33, ~0x5B, ~0x5F, ~0x70, ~0x7F, ~0x7B
};

void io_pwm_init(void) {
// i/o
	SEGMENT7_DDR = 0xFF;
	D_SEGMENT7_DDR = 0x0F; //PB0-PB3

	D_SEGMENT7_DDR &= ~(1 << DDB7); // ustawienie PB7 jako wejscie
	D_SEGMENT7_PORT |= (1 << PORTB7); //w³aczenie rezystor pullup

	//tutaj to samo co wy¿ej z tylko ¿e z PC0-PC3
	KEYPAD_DDR &= ~(1 << DDC0) & ~(1 << DDC1) & ~(1 << DDC2) & ~(1 << DDC3);
	KEYPAD_PORT |= (1 << PORTC0) | (1 << PORTC1) | (1 << PORTC2) | (1 << PORTC3);

// przerwania
	PCICR |= (1 << PCIE0); // w³¹cza przerwania od zmiany stanu pinów w porcie B ale dla PE0
	PCMSK0 |= (1 << PCINT7); // maskowanie przerwañ dla PB7 (rejestr maski przerwañ)
// pwm

	BUZZER_DDR |= (1 << DDE0); // PE0 - wyjœcie

	TCCR1A |= (1 << WGM11); //ustawia bit WGM11 w tym rejestrze,aby wybraæ tryb pracy Fast PWM
	TCCR1B |= (1 << WGM13) | (1 << WGM12) | (1 << CS10); //to samo co wy¿ej plus preskaler na 1
	ICR1 = 255; // liczy od 0 do 255 - cykl PWM
	OCR1A = duty_cycle;

	TIMSK1 |= (1 << OCIE1A) | (1 << TOIE1); //w³¹cza przerwania porównawcze i przepe³nienia dla timer1

}

void delay_ms(uint16_t ms)
{
	for (uint16_t i = 0; i < ms; i++)
	{
		_delay_ms(1);
	}
}
//WYŒWIETLANIE POJEDYÑCZEJ CYFRY
void display_digit(uint8_t digit, uint8_t position) {
	SEMGENT7_PORT = 0xFF;

	if (digit <= 9) {
		SEMGENT7_PORT = segment_digits[digit];
	}
	D_SEGMENT7_PORT = ~(1 << position);

	delay_ms(5);
	D_SEGMENT7_PORT = 0xFF;
}
//PODZIELENIE LICZBY MAX CZTEROCYFROWEJ NA POJEDYÑCZE I WYŒWIETLENIE
void display_number(uint16_t number) {
	display_digit((number / 1000) % 10, PB3);
	display_digit((number / 100) % 10, PB2);
	display_digit((number / 10) % 10, PB1);
	display_digit(number % 10, PB0);
}

//CYKL PRACY BUZZERA
void set_pwm_duty_cycle(uint8_t duty_cycle)
{
	OCR1A = duty_cycle;
}

//PRZERWANIE COMPARATORA A(COMPA) dla TIMER1 - ustawienie stanu porte0 na wysoki (w³¹cza buzzer)
ISR(TIMER1_COMPA_vect)
{
	if (buzzer_on)
	{
		BUZZER_PORT|= (1 << PORTE0);
	}
}
//PRZERWANIE PRZEPE£NIENIA TIMERA 1 - ustawienie stanu porte0 niski (wy³¹cza buzzer)
ISR(TIMER1_OVF_vect)
{
	BUZZER_PORT &= ~(1 << PORTE0);
}

void metronome_init(void)
{
	sei(); // globalnie w³¹cza przerwania
}
// FUNKCJA POMOCNICZA - odpowiada za w³aczanie PWM i wy³¹czanie
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
// DEBOUNCING PRZYCISKÓW - eliminuje drganie ze styków (wielokrotne wciœniêcia)
uint8_t debounce(uint8_t pinA, uint8_t pinB)
{
	if (!(pinA & (1 << pinB))) //spr stan niski
	{
		delay_t1(50); //stabilizacja
		if (!(pinA & (1 << pinB))) //ponowne sprawdzenie
		{
			return 1; //zwraca 1 jeœli pin jest nadal niski
		}
	}
	return 0; //zwraca 0 gdy zosta³ zmieniony stan
}

void keypad()
{
	//flaga zachowuje poprzedni stan ka¿dego pinu do wykrycia zmian syngalu
    static uint8_t prev_state_C0 = 1;
    static uint8_t prev_state_C1 = 1;
    static uint8_t prev_state_C2 = 1;
    static uint8_t prev_state_C3 = 1;

    uint8_t current_state_C0 = debounce(PINC, PINC0);
    uint8_t current_state_C1 = debounce(PINC, PINC1);
    uint8_t current_state_C2 = debounce(PINC, PINC2);
    uint8_t current_state_C3 = debounce(PINC, PINC3);

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
//PRZERWANIE ZMIANY STANU PB7(przycisk na p³ytce) - w³¹cz/wy³¹cz buzzer
ISR(PCINT0_vect)
{
	static uint8_t prev_button_state = 1; // ustawienie flagi
	uint8_t button_pressed = debounce(PINB, PINB7);

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
