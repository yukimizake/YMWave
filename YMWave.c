/*
 * YMWave.c
 *
 * Created: 05/04/2014 20:11:14
 *  Author: Bob
 */ 
#define F_CPU 16000000

#include <avr/io.h>
#include <util/delay.h>
#include <stdlib.h>
#include <avr/interrupt.h>
#include "hd44780.h"
#include "MIDI.h"

//Bitwise operation macros
#define CLR(x,y) (x &= ~_BV(y))
#define SET(x,y) (x |= _BV(y))
#define ISSET(sfr, bit) (_SFR_BYTE(sfr) & _BV(bit))
#define ISCLR(sfr, bit) (!(_SFR_BYTE(sfr) & _BV(bit)))

//IO macros
#define __BCPORT__ PORTC
#define __BC1__ PORTC7
#define __BDIR__ PORTC6

#define BUTTON_INC_PRESSED !ISSET(PINC,PINC0)
#define BUTTON_DEC_PRESSED !ISSET(PINC,PINC1)
#define BUTTON_OK_PRESSED !ISSET(PINC,PINC2)
#define BUTTON_BACK_PRESSED !ISSET(PIND,PIND3)

///////////////////
// Voice parameters

//voicing
uint8_t noteA = 0;
uint8_t noteB = 0;
uint8_t noteC = 0;
int periodA = 0;
int periodB = 0;
int periodC = 0;

//envelope
uint8_t AmaxVolume = 15;
uint8_t BmaxVolume = 15;
uint8_t CmaxVolume = 15;

//polyphony
uint8_t note_age[] = {0,0,0}; // note age for polyphony management

///////////////////
// Patch parameters

uint8_t patch_detune_value = 3;

////////////////////
// Global parameters

uint8_t global_midi_input_channel = 0; //0x0 to 0xF


////////////
// Constants

// Pitch table
const int tp[] = {0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 4050, 3822, 3608, 3405, 3214,
	3034, 2863, 2703, 2551, 2408, 2273, 2145, 2025, 1911, 1804, 1703,
	1607, 1517, 1432, 1351, 1276, 1204, 1136, 1073, 1012, 956, 902, 851,
	804, 758, 716, 676, 638, 602, 568, 536, 506, 478, 451, 426, 402, 379,
	358, 338, 319, 301, 284, 268, 253, 239, 225, 213, 201, 190, 179, 169,
	159, 150, 142, 134, 127, 119, 113, 106, 100, 95, 89, 84, 80, 75, 71,
	67, 63, 60, 56, 53, 50, 47, 45, 42, 40, 38, 36, 34, 32, 30, 28, 27,
	25, 24, 22, 21, 20};
	
// Pitch table (for envelope oscillation)
const int envTp[] = {3822, 3608, 3405, 3214, 3034, 2863, 2703, 2551, 2408,
	2273, 2145, 2025, 1911, 1804, 1703, 1607, 1517, 1432, 1351, 1276, 1204,
	1136, 1073, 1012, 956, 902, 851, 804, 758, 716, 676, 638, 602, 568, 536,
	506, 478, 451, 426, 402, 379, 358, 338, 319, 301, 284, 268, 253, 239,
	225, 213, 201, 190, 179, 169, 159, 150, 142, 134, 127, 119, 113, 106,
	100, 95, 89, 84, 80, 75, 71, 67, 63, 60, 56, 53, 50, 47, 45, 42, 40, 38,
	36, 34, 32, 30, 28, 27, 25, 24, 22, 21, 20, 19, 18, 17, 16, 15, 14, 13,
	13, 12, 11, 11, 10, 9, 9, 8, 8, 7, 7, 7, 6, 6, 6, 5, 5, 5, 4, 4, 4, 4,
	4, 3, 3, 3, 3, 3, 2};


///////////////
// Functions //
///////////////

//////////////////////////////////////////////////////////////////////////
// Setup

void setup_io()
{
	DDRA = 0xFF; //YM2149 data bus
	DDRB = 0xFE; //LCD data + control + contrast (PB3)
	DDRC = 0xF0; //Buttons on PC0 to PC2, Mono command on PC4, YM2149 control signals on PC5 to PC7
	DDRD = 0xF1; //MIDI In, Stereo switch, BACK button and activity LED
	
	SET(PORTC,PORTC0);//pull-up resistor on
	SET(PORTC,PORTC1);//pull-up resistor on
	SET(PORTC,PORTC2);//pull-up resistor on
	
	SET(PORTD,PORTD2);//pull-up resistor on
	SET(PORTD,PORTD3);//pull-up resistor on
	
	SET(PORTD,PORTD4); //LCD backlight on
	
	//YM2149 reset
	CLR(PORTC,PORTC5);
	_delay_ms(1);
	SET(PORTC,PORTC5);
	_delay_ms(500);
	CLR(PORTC,PORTC4);
	
	ym_send(0x07, 0b00111000);
	ym_send(0x08, 0x0f);
	ym_send(0x09, 0x0f);
	ym_send(0x0A, 0x0f);
}

void setup_uart()
{
	UCSRC = (1 << URSEL) | (1 << UCSZ0) | (1 << UCSZ1); // Use 8-bit character sizes - URSEL bitset to select the UCRSC register
	UCSRB = (1 << RXEN) | (1 << RXCIE); // Turn on the reception circuitry & enable the byte received interrupt
	cli(); // Disable interrupts until the boot sequence is finished
	
	// 31250 bauds
	UBRRH = 0x00;
	UBRRL = 0x1F;
}

void setup_pwm()
{
	//OC0 Vee : LCD Contrast
	TCCR0 = (1<<COM01)|(1<<WGM01)|(1<<WGM00)|(1<<CS00);
	OCR0 = 0x40;
}

void setup_lcd()
{
	_delay_ms(100);
	lcd_init();
	_delay_ms(20);
	lcd_clrscr();
}

void setup_interrupts()
{
	//INT1 on falling edge
	MCUCR |= ( 1 << ISC11 );
	//enable INT1
	GICR |= ( 1 << INT1 );
	//cli();
}

//////////////////////////////////////////////////////////////////////////
// Utility

void flash_led() 
{
	SET(PORTD,PORTD7);
	_delay_us(100);
	CLR(PORTD,PORTD7);
}

void flash_lcd()
{
	CLR(PORTD,PORTD4);
	_delay_ms(10);
	SET(PORTD,PORTD4);
}

void debug_print(char *text)
{
	lcd_goto(0x4c);
	lcd_puts(text);
	_delay_ms(300);
	lcd_goto(0x4c);
	lcd_puts("----");
}

void print_number(int n)
{
	char buffer[4];
	itoa(n, buffer, 16);
	lcd_puts(buffer);
}

ISR(INT1_vect) // Back button
{
	flash_lcd();
	_delay_ms(1); // Debouncing
	if (BUTTON_BACK_PRESSED) // Debouncing too
	{
		// Kill all notes
		ym_send(0x00, 0);
		ym_send(0x01, 0);
		ym_send(0x02, 0);
		ym_send(0x03, 0);
		ym_send(0x04, 0);
		ym_send(0x05, 0);
		debug_print("BACK");
		lcd_clrscr();
	}
}

//////////////////////////////////////////////////////////////////////////
// Synth

void ym_send(uint8_t address, uint8_t data)
{
	PORTA = address;
	
	//validate addess
	__BCPORT__ |=   (1 << __BDIR__) | (1 << __BC1__);
	_delay_us(1);
	__BCPORT__ &= ~((1 << __BDIR__) | (1 << __BC1__));
	
	_delay_us(1);
	
	PORTA = data;
	
	//validate data
	SET(__BCPORT__,__BDIR__);
	_delay_us(1);
	CLR(__BCPORT__,__BDIR__);
}

void play_note(uint8_t note, uint8_t velo, uint8_t chan)
{
	
	if (note < 24) return;
	if (chan == 0)
	{
		noteA = note;
		periodA = tp[note] + patch_detune_value;
		uint8_t LSB = ( periodA & 0x00FF);
		uint8_t MSB = ((periodA & 0x0F00) >> 8);
		cli();
		ym_send(0x00, LSB);
		ym_send(0x01, MSB);
		ym_send(0x08, velo >> 3); //scale 7 bits to 4 bits
		//ym_send(0x08, AmaxVolume); //can be set to 0 by envelope mode note off
		sei();
	}
	else if (chan == 1)
	{
		noteB = note;
		periodB = tp[note];
		uint8_t LSB = ( periodB & 0x00FF);
		uint8_t MSB = ((periodB & 0x0F00) >> 8);
		cli();
		ym_send(0x02, LSB);
		ym_send(0x03, MSB);
		ym_send(0x09, velo >> 3); //scale 7 bits to 4 bits
		//ym_send(0x09, BmaxVolume); //can be set to 0 by envelope mode note off
		sei();
	}
	else if (chan == 2)
	{
		noteC = note;
		periodC = tp[note] - patch_detune_value;
		uint8_t LSB = ( periodC & 0x00FF);
		uint8_t MSB = ((periodC & 0x0F00) >> 8);
		cli();
		ym_send(0x04, LSB);
		ym_send(0x05, MSB);
		ym_send(0x0A, velo >> 3); //scale 7 bits to 4 bits
		//ym_send(0x0A, CmaxVolume); //can be set to 0 by envelope mode note off
		sei();
	}
}

void stop_note(uint8_t note, uint8_t chan)
{
	if (chan == 0 && note == noteA)
	{
		noteA = periodA = 0;
		cli();
		ym_send(0x00, 0);
		ym_send(0x01, 0);
		sei();
	}
	else if (chan == 1 && note == noteB)
	{
		noteB = periodB = 0;
		cli();
		ym_send(0x02, 0);
		ym_send(0x03, 0);
		sei();
	}
	else if (chan == 2 && note == noteC)
	{
		noteC = periodC = 0;
		cli();
		ym_send(0x04, 0);
		ym_send(0x05, 0);
		sei();
	}
}

//////////////////////////////////////////////////////////////////////////
//MIDI management

//midi vars
uint8_t midi_command = 0x00;
uint8_t midi_byte2 = 0x00;
uint8_t midi_byte3 = 0x00;
uint8_t midi_message_ready = 0;
uint8_t midi_clock_counter = 0;

//buffer processing
void process_midi_buffer()
{	
	if (midi_command == 0x90)
	{
		if (midi_byte3 == 0x00)
			midi_command = 0x80;
		else
		{
			play_note(midi_byte2,midi_byte3,0);
			play_note(midi_byte2,midi_byte3,1);
			play_note(midi_byte2,midi_byte3,2);
		}
	}
	
	if (midi_command == 0x80)
	{
		stop_note(midi_byte2,0);
		stop_note(midi_byte2,1);
		stop_note(midi_byte2,2);
	}
}

//midi input interrupt
ISR(USART_RXC_vect) // MIDI data input
{
	char ReceivedByte;
	ReceivedByte = UDR; // Fetch the received byte value into the variable "ByteReceived"
	
	
	//gérer les successions de valeurs (2 x 8 bits) si commande déjà présente en buffer
	
	
	if (ReceivedByte >> 7) //bit 7 high: midi command
	{
		if (ReceivedByte > 0xEF) //system message 
		{
			//si 0xF0 reçu tout ignorer jusqu'au 0xF7
			
			if (ReceivedByte = 0xF8) //MIDI clock
			{
				//TODO : ajouter un timeout qui reset le compteur à 0 (genre 500 ms)
				
				if (midi_clock_counter-- == 0)
				{
					flash_led();
					midi_clock_counter = 23;
				}
			}
		}
		else //update MIDI command
		{
			midi_byte2 = 0x00;
			midi_byte3 = 0x00;
			midi_command = ReceivedByte;
		}
	}
	else if (midi_byte2 == 0x00) //managing value bytes
	{ 
		midi_byte2 = ReceivedByte;
		midi_byte3 = 0x00;
	}
	else if (midi_byte3 == 0x00)
	{
		midi_byte3 = ReceivedByte;
		
		process_midi_buffer();
		midi_byte2 = 0x00;
		midi_byte3 = 0x00;
	}
	
}

//////////////////////////////////////////////////////////////////////////
// Main loop
int main (void)
{
	setup_io();
	setup_uart();
	setup_pwm();
	setup_lcd();
	setup_interrupts();

	lcd_puts(" CUIR ");
	_delay_ms(200);
	lcd_puts("CUIR ");
	_delay_ms(200);
	lcd_puts("CUIR");
	_delay_ms(200);
	lcd_goto(40);  //Position 40 is the start of line 2
	lcd_puts("  MOUSTACHE !");
	_delay_ms(400);
	
	lcd_clrscr();
	
	flash_led();
	
	//Test tone
	play_note(69,127,1);
	_delay_ms(100);
	stop_note(69,1);
	
	//Start interrupts
	sei();

	for (;;)
	{
		//it's good to do nothing
	}
	
	return(0);
}