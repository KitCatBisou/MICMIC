#include <avr/interrupt.h>
#define F_CPU 16000000UL
#include <util/delay.h>
#include <stdio.h>
#include <stdlib.h>


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


volatile uint16_t last_capture = 0;
volatile uint16_t current_capture = 0;
volatile uint16_t pulse_ticks = 0;
volatile uint16_t rpm = 0;
volatile uint8_t  rpm_timeout = 0; // To detect if motor stopped

/*
   USART VARIABLES

*/
const unsigned char mode[] = {0b10100001, 0b10010010, 0b10001000, 0b10000010, 0b11000001};
volatile unsigned char flagMode = 0; //PC(0) / switches(1) / potentiometer(2) / step motor(3)
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
   STEP MOTOR VARIABLES

*/

const steps[] = {0b00001001,0b00001100,0b00000110,0b00000011};
int8_t x = 0;
int16_t current_pos = 0;   // Where the motor is now
char buffer[10];           // To store the typed numbers
uint8_t buf_index = 0;     // To keep track of where we are writing in the buffer

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

	DDRE = 0b00001111;
	PORTE = 0b00000000;


	// USART Setup
	UBRR1H = 0; // USART1
	UBRR1L = 207; //Baud Rate = 9600
	UCSR1A = (1 << U2X1); //double speed
	UCSR1B = (1 << RXCIE1) | (1 << RXEN1) | (1 << TXEN1); //habilita rececao, transmissao e interrupçao rececao
	UCSR1C = (1 << UCSZ11) | (1 << UCSZ10) | (1 << USBS1);

	// ADC Setup
	ADMUX = 0b00100000; // AREF, direita, canal 0
	ADCSRA = 0b10000111; // ADEN, Prescaler 128

	DDRD &= ~(1 << PD4); // Set PD4 as Input
	PORTD |= (1 << PD4); // Enable Pull-up (Good for optical sensors)

	// Normal Mode, Output Disconnected
	TCCR1A = 0;

	// Noise Canceler ON, Rising Edge, Prescaler 256
	TCCR1B = (1 << ICNC1) | (1 << ICES1) | (1 << CS12);

	// Enable Timer 1 Interrupts
	TIMSK |= (1 << TICIE1) | (1 << TOIE1);

	sei(); //activates flag I of SREG
}

ISR(TIMER0_COMP_vect){
	flag5ms=1;
	if (flagInv > 0) {
		flagInv--;
		PORTC = digits[10];
		// When it hits 1, we restart the motor
		if (flagInv == 1) {
			Inv(); // Call the function to flip the bits
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
    int16_t temp_val;

    // MODE 4 (RPM)
    if (flagMode == 4) {
       switch(current_display) {
          case 0: PORTA = displays[0]; PORTC = digits[rpm % 10]; break;
          case 1: PORTA = displays[1]; PORTC = digits[(rpm / 10) % 10]; break;
          case 2: PORTA = displays[2]; PORTC = digits[(rpm / 100) % 10]; break;
          case 3: PORTA = displays[3]; PORTC = digits[(rpm / 1000) % 10]; break;
       }
    }
    // --- MODES 0, 1, 2, 3: Standard Display (2 Digits + Mode Letter) ---
    else {
       if (flagMode == 3) {
          // Stepper Motor Math
          temp_val = current_pos;
          if (temp_val < 0) {
             signal = 1;
             temp_val = -temp_val;
          } else {
             signal = 0;
          }
          if (temp_val > 99) temp_val = 99;

          num_d0 = temp_val % 10;
          num_d1 = temp_val / 10;
       }
       else {
          // DC Motor Math (Modes 0, 1, 2)
          num_d0 = motor_speed % 10;
          num_d1 = motor_speed / 10;
          if(num_d1 == 10){ num_d1 = 9; num_d0 = 9; }
       }

       //Modes 0, 1, 2, 3
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
             if (signal == 1) PORTC = digits[11]; // Minus
             else PORTC = digits[10];             // Blank
             break;
          case 3:
             PORTA = displays[3];
             PORTC = mode[flagMode]; // Show d, S, A, or M
             break;
       }
    }

    // INCREMENT FOR EVERYONE
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

/*
 STEP MOTOR

 */

void step_once(int8_t direction) {
	if (direction > 0) {
		// Move Right (+1)
		x++;
		if (x > 3) x = 0;
		PORTE = steps[x];
		current_pos++;
	}
	else {
		// Move Left (-1)
		x--;
		if (x < 0) x = 3; // char is signed, so check for < 0
		PORTE = steps[x];
		current_pos--;
	}
	_delay_ms(25);
}

/*
 INTERRUPT FOR RPM

 */

// ISR for Input Capture (When sensor sees a pulse on PD4)
ISR(TIMER1_CAPT_vect) {
	// Read the timer value
	current_capture = ICR1;

	// Calculate time difference
	pulse_ticks = current_capture - last_capture;

	// Save current as last for next time
	last_capture = current_capture;

	//Calculate RPM
	if (pulse_ticks > 0) {
		rpm = 3750000UL / pulse_ticks;
	}

	rpm_timeout = 0; // Reset timeout (motor is moving)
}

// ISR for Timer Overflow (Detects if motor stopped)
ISR(TIMER1_OVF_vect) {
	rpm_timeout++;
	if (rpm_timeout > 5) { // If ~250ms passes with no pulse
		rpm = 0;           // Motor is stopped
		rpm_timeout = 5;   // Clamp
	}
}

/*
 MAIN

 */


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

		if(rxUSART.receiver_buffer == 'm' || rxUSART.receiver_buffer == 'M'){
			flagMode = 3;
			rxUSART.receive = 0;
		}
		if(rxUSART.receiver_buffer == 'v' || rxUSART.receiver_buffer == 'V'){
			flagMode = 4;
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
		else if (flagMode == 1 || flagMode == 4) {
			input = PINA & 0b0111111;
		}

		else if	(flagMode == 2) {
			//Get Speed from Assembly
			adc_val = read_adc_avg();

			// Convert 0-255 (ADC) to 0-100 (Speed)
			motor_speed = (adc_val * 100) / 255;

			//Update Motor
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

		else if (flagMode == 3) {
			if (rxUSART.receive == 1) {
				char c = rxUSART.receiver_buffer;
				rxUSART.receive = 0; // Clear flag

				// Reset Reference
				if (c == 'R' || c == 'r') {
					current_pos = 0;
				}

				// Manual Step Up
				else if (c == 'Z' || c == 'z') {
					step_once(1);
				}

				// Manual Step Down
				else if (c == 'X' || c == 'x') {
					step_once(-1);
				}

				// "Enter" key (\r is Enter, \n is New Line)
				else if (c == '\r' || c == '\n') {
					buffer[buf_index] = '\0'; // Close the string
					int target = atoi(buffer); // Convert text to number

					// Move until we reach the target
					while (current_pos != target) {
						if (current_pos < target) {
							step_once(1);
						} else {
							step_once(-1);
						}
					}
					buf_index = 0; // Reset buffer for next number
				}
				// 5. Capture Numbers and Minus sign
				else {
					if (buf_index < 9) { // Prevent overflow
						buffer[buf_index] = c;
						buf_index++;
					}
				}
			}
		}

		if (flagMode != 2 && flagMode != 3) { //if not in potentiometer mode or step motor mode
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