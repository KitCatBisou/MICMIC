#include <avr/interrupt.h>
#include <util/delay.h>
#include <stdio.h>
#define F_CPU 16000000UL

/*
   MOTOR AND DISPLAY VARIABLES

*/
const unsigned char digits[] = {0xc0, 0xf9, 0xa4, 0xb0, 0x99, 0x92, 0x82, 0xf8, 0x80, 0x90, 0xFF, 0XBF}; //0-9, blank, minus signal
const unsigned char displays[] = {0b11000000, 0b10000000, 0b01000000, 0b00000000};

volatile unsigned char flag5ms = 0;
volatile unsigned char flagStop = 0;
volatile unsigned char flagInv = 0;
volatile unsigned char motor_speed = 0;

unsigned char speed;
unsigned char num_d1 = 0; //current number on display 1
unsigned char num_d0 = 0; //current number on display 0
unsigned char signal = 0;
unsigned char current_display = 0;

/*
   USART VARIABLES

*/
const unsigned char mode[] = {0b10100001, 0b10010010, 0b10001000};
volatile unsigned char flagMode = 0; //PC(0) / switches(1) / potentiometer(2)
unsigned char input = 0;

typedef struct USARTRX {
	char receiver_buffer;
	unsigned char status;
	unsigned char receive: 1; //reserva 1 bit (sem nada reserva 1 byte)
	unsigned char error: 1;
	}USARTRX_st;

volatile USARTRX_st rxUSART = {0, 0, 0, 0}; // inicializa variável
char transmit_buffer[20];

extern uint8_t read_adc_avg(void); //assembly function

/*
   INITS

*/

void init(void) {
	DDRA = 0b11000000;
	PORTA = 0b11000000;

	DDRB = 0b11100000;
	PORTB = 0b01000000;	//define motor direction

	DDRC = 0xFF;
	PORTC = 0xFF;

	DDRF = 0x00; // Set Port F as Input for ADC
	PORTF = 0x00; // No Pull-up

	OCR0 = 77; //5ms
	TCCR0 = 0b00001111; //modo CTC, prescaler = 1024
	TIMSK |= 0b00000010; //TC0

	OCR2 = 0; //motor speed = 0
	TCCR2 = 0b01100011;		// Modo Phase Correct, prescaler 64 (490Hz)


	// USART Setup
	UBRR1H = 0; // USART1
	UBRR1L = 207; //Baud Rate = 9600
	UCSR1A = (1 << U2X1); //double speed
	UCSR1B = (1 << RXCIE1) | (1 << RXEN1) | (1 << TXEN1); //habilita rececao, transmissao e interrupçao rececao
	UCSR1C = (1 << UCSZ11) | (1 << UCSZ10) | (1 << USBS1);

	// ADC Setup
	ADMUX = 0b00100000; // AREF, direita, canal 0
	ADCSRA = 0b10000111; // ADEN, Prescaler 128

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


/*
 MOTOR SETUP
*/

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
			if (signal == 1) {
				PORTC = digits[11];
			}
			else {
				PORTC = digits[10];
			}
			break;

		case 3:
			PORTA = displays[3];
			PORTC = mode[flagMode]; //either 0, 1 or 2 which is "d", "S" or "A" on display
	}
	current_display++;
	if(current_display == 4) {
		current_display = 0;
	}
}

/*
USART SETUP
*/

void send_message(char *buffer){
	unsigned char i = 0;

	while(buffer[i] != '\0'){
		while((UCSR1A & (1 << UDRE1)) == 0);
		UDR1 = buffer[i];
		i++;
	}
}

ISR(USART1_RX_vect){
	rxUSART.status = UCSR1A; //flag para erros

	if (rxUSART.status & ((1 << FE1) | (1 << DOR1) | (1 >> UPE1))){
		rxUSART.error = 1;
	}

	rxUSART.receiver_buffer = UDR1;
	rxUSART.receive = 1;
}



int main(void) {

	init();

	unsigned char flag = 0; //flag for debounce
	unsigned char adc_val = 0; //Variable for ADC result

	while(1) {


		if(rxUSART.receiver_buffer == 'd' || rxUSART.receiver_buffer == 'D'){
			flagMode = 0;
			rxUSART.receive = 0;
		}

		if(rxUSART.receiver_buffer == 's' || rxUSART.receiver_buffer == 'S'){
			flagMode = 1;
			rxUSART.receive = 0;
		}

		if(rxUSART.receiver_buffer == 'a' || rxUSART.receiver_buffer == 'A'){
			flagMode = 2;
			rxUSART.receive = 0;
		}


		if(flagMode == 0){
			if(rxUSART.receive == 1){ //if we have new data
				if (rxUSART.error == 1) { //if theres an error
					rxUSART.error = 0;
				}
				else {
					input = rxUSART.receiver_buffer;
				}
				rxUSART.receive = 0;
			}
		}
		else if (flagMode == 1) {
			input = PINA & 0b0111111;
		}

		else {
			// 1. Get Speed from Assembly
			adc_val = read_adc_avg();

			// 2. Map 0-255 (ADC) to 0-100 (Speed)
			motor_speed = (adc_val * 100) / 255;

			// 3. Update Motor PWM
			speed = (motor_speed * 255) / 100;
			OCR2 = speed;

			//invert with sw5
			if ( (PINA & 0b00010000) == 0 ) { //mascara para o sw5
				_delay_ms(50); // Debounce
				if(flag == 0 && flagStop == 0){
					flag = 1;
					flagInv = 50; // counts 250ms
					OCR2 = 0;
				}
			} else {
				flag = 0; // Reset flag when button released
			}
		}

		if (flagMode != 2) { //if not in potentiometer mode
			switch(input){
				case 0b00111110: //SW1 & '+' inc 5%
				case '+':
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

			case 0b00111101: //SW2 & '-' dec 5%
			case '-':
				_delay_ms(50);
				if(flag == 0){
					flag = 1;
					if(flagStop == 1){
						flagStop = 0;
						speed = (motor_speed * 255) / 100;
						OCR2 = speed;
					}
					else {
						if(motor_speed > 0){
							motor_speed -= 5;
							speed = (motor_speed * 255) / 100;
							OCR2 = speed;
						}
					}
				}
			break;

			case 0b00111011: //SW3 and '1' puts motor speed at 25%
			case '1':
				_delay_ms(50);
				if(flag == 0) {
					flag = 1;
					flagStop = 0; // Added safety un-stop
					motor_speed = 25;
					speed = (motor_speed * 255) / 100;
					OCR2 = speed;
				}
			break;

			case 0b00110111: //SW4 and '2' puts motor speed at 50%
			case '2':
				_delay_ms(50);
				if(flag == 0) {
					flag = 1;
					flagStop = 0;
					motor_speed = 50;
					speed = (motor_speed * 255) / 100;
					OCR2 = speed;
				}
			break;

			case 0b00101111:  //SW5, 'i' and "I" inverts speed
			case 'I':
			case 'i':
				_delay_ms(50);
				if(flag == 0 && flagStop == 0){
					flag=1;
					flagInv=50; //contador para contar 250ms entre motor parar e inverter
					OCR2=0;
				}
			break;

			case 0b00011111:  //SW6, 'p' and 'P' stops motor
			case 'P':
			case 'p':
				_delay_ms(50);
				if(flag == 0){
					flag = 1;
					flagStop = 1;
					motor_speed = 0;
					speed = (motor_speed * 255) / 100;
					OCR2 = speed;
				}
			break;

			case 'b':
			case 'B':
				if(signal == 1){
					sprintf(transmit_buffer,"Motor Speed: -%d\r\n",motor_speed);
				}
				else{
					sprintf(transmit_buffer,"Motor Speed: %d\r\n",motor_speed);
				}
				send_message(transmit_buffer);
				rxUSART.receive = 0;
			break;

			default:
				flag = 0;
				break;
		}
	}

		input = 0; //reset every loop

		if(flag5ms == 1){
			flag5ms = 0;

			update_display();
		}
	}
	return 0;
}