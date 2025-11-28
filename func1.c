#include <avr/interrupt.h>
#include <util/delay.h>
#define F_CPU 16000000UL

const unsigned char digits[] = {0xc0, 0xf9, 0xa4, 0xb0, 0x99, 0x92, 0x82, 0xf8, 0x80, 0x90, 0xFF, 0XBF}; //0-9, blank, minus signal
const unsigned char displays[] = {0b11000000, 0b10000000, 0b01000000};

volatile unsigned char flag5ms = 0;
volatile unsigned char flagStop = 0;
volatile unsigned char flagInv = 0;
volatile unsigned char motor_speed = 0;

unsigned char speed;
unsigned char num_d1 = 0; //current number on display 1
unsigned char num_d0 = 0; //current number on display 0
unsigned char signal = 0;
unsigned char switches = 0;
unsigned char current_display = 0;

void init(void) {
	DDRA = 0b11000000;
	PORTA = 0b11000000;

	DDRB = 0b11100000;
	PORTB = 0b01000000;	//define motor direction

	DDRC = 0xFF;
	PORTC = 0xFF;

	OCR0 = 77; //5ms
	TCCR0 = 0b00001111; //modo CTC, prescaler = 1024
	TIMSK |= 0b00000010; //TC0

	OCR2 = 0; //motor speed = 0
	TCCR2 = 0b01100011;		// Modo Phase Correct, prescaler 64 (490Hz)

	sei(); //activates flag I of SREG
}

ISR(TIMER0_COMP_vect){
	flag5ms=1;
	if (flagInv > 0) {
		flagInv--;
		PORTC = digits[10];
		// When it hits 1, we are ready to restart the motor
		if (flagInv == 1) {
			Inv(); // Call the function to flip the bits
			// Restore speed here if needed, or let the loop handle it
		}
	}
}

void Inv(void)
{
	if(signal == 0){
		signal = 1;
		PORTB = 0b00100000;
	}
	else{
		signal = 0;
		PORTB = 0b01000000;
	}
	speed = (motor_speed * 255) / 100; //formula to put the motor speed percentage on OCR2
	OCR2 = speed;
}

void update_display(void) {
	num_d0 = motor_speed % 10; //puts last digit of speed on display 0
	num_d1 = motor_speed / 10; //puts first digit of speed on display 1
	if(num_d1 == 10){ //if speed == 100, puts 99 on display
		num_d1 = 9;
		num_d0 = 9;
	}
	switch(current_display){
		case 0:
		PORTA = displays[0];
		PORTC = digits[num_d0];
		break;
		case 1:
		PORTA = displays[1];
		PORTC = digits[num_d1];
		break;
		case 2:
		PORTA = displays[2];
		// If signal is 1 (negative), show minus, else blank
		if (signal == 1)
		PORTC = digits[11];
		else
		PORTC = digits[10];
		break;
	}
	current_display++;
	if(current_display == 3) {
		current_display = 0;
	}
}



int main(void) {

	init();

	unsigned char flag = 0;

	while (1) {
		switches = PINA & 0b0111111;

		switch(switches){
			case 0b00111110:	//SW1	inc 5%
			_delay_ms(50);
			if(flag == 0){
				flag = 1;
				if(flagStop == 1){
					flagStop = 0;
					speed = (motor_speed * 255) / 100;
					OCR2 = speed;
				}
				else{
					if(motor_speed < 100){
						motor_speed += 5;
						speed = (motor_speed * 255) / 100;
						OCR2 = speed;
					}
				}
			}
			break;
			case 0b00111101: //SW2	dec 5%
			_delay_ms(50);
			if(flag == 0){
				flag = 1;
				if(flagStop == 1){
					flagStop = 0;
					speed = (motor_speed * 255) / 100;
					OCR2 = speed;
				}
				else{
					if(motor_speed > 0){
						motor_speed -= 5;
						speed = (motor_speed * 255) / 100;
						OCR2 = speed;
					}
				}
			}

			break;
			case 0b00111011: //SW3 puts motor speed at 25%
			_delay_ms(50);
			if(flag == 0) {
				flag = 1;
				flagStop = 0; // Added safety un-stop
				motor_speed = 25;
				speed = (motor_speed * 255) / 100;
				OCR2 = speed;
			}
			break;
			case 0b00110111: //SW4 puts motor speed at 50%
			_delay_ms(50);
			if(flag == 0) {
				flag = 1;
				flagStop = 0;
				motor_speed = 50;
				speed = (motor_speed * 255) / 100;
				OCR2 = speed;
			}
			break;

			case 0b00101111:  //SW5 inverts speed
			_delay_ms(50);
			if(flag == 0 && flagStop == 0){
				flag=1;
				flagInv=50; //contador para contar 250ms entre motor parar e inverter
				OCR2=0;
			}
			break;

			case 0b00011111:  //SW6 stops motor
			_delay_ms(50);
			if(flag == 0){
				flag = 1;
				flagStop = 1;
				motor_speed = 0;
				speed = (motor_speed * 255) / 100;
				OCR2 = speed;
			}
			break;
			default:
			flag = 0;
			break;
		}

		if(flag5ms == 1){
			flag5ms = 0;

			update_display();
		}
	}
	return 0;
}