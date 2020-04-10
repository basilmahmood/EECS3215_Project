#include <stdio.h>
#include <stdbool.h>
#include "LPC802.h"
#include "fsl_i2c.h"
#include "fsl_usart.h"
// Defines

#define LM75_I2CAddress (0x4C)

// Push button gpio connections
#define BUTTON1 (12)
#define BUTTON2 (11)
#define BUTTON3 (13)

// Define values for I2C registers that aren't in the header file.
// Table 195 of LPC802 User Manual

#define MSTCTL_CONTINUE (1UL << 0) // Bit 0 of MSTCTL set ("Main" or "Primary")
#define MSTCTL_START (1UL << 1) // Bit 1 of MSTCTL set ("Main" or "Primary")
#define MSTCTL_STOP (1UL << 2) // Bit 2 of MSTTCL set ("Main" or "Primary")
#define CTL_SLVCONTINUE (1UL << 0) // Bit 0: Secondary level (SLV) Continue
#define CTL_SLVNACK (1UL << 1) // Bit 1: Secondary Level (SLV) Acknowledge

#define PRIMARY_STATE_MASK (0x7<<1) // bits 3:1 of STAT register for Main / Primary
#define I2C_STAT_MSTST_IDLE ((0b000)<<1) // Main Idle: LPC802 user manual table 187
#define I2C_STAT_MSTST_RXRDY ((0b001)<<1) // Main Receive Ready " "
#define I2C_STAT_MSTST_TXRDY ((0b010)<<1) // Main Transmit Ready " "
#define I2C_STAT_MSTST_NACK_ADD ((0b011)<<1)// Main Ack Add " "
#define I2C_STAT_MSTST_NACK_DATA ((0b100)<<1)// Main Ack signal data ” ”

// Performs the necessary initialization to use the I2C
void i2c_setup() {
  SYSCON->SYSAHBCLKCTRL0 |= (SYSCON_SYSAHBCLKCTRL0_SWM_MASK); // enable switch matrix.
  SWM0->PINASSIGN.PINASSIGN5 &= ~(SWM_PINASSIGN5_I2C0_SCL_IO_MASK |
  SWM_PINASSIGN5_I2C0_SDA_IO_MASK); // clear 15:0
  SWM0->PINASSIGN.PINASSIGN5 |= ((16<<SWM_PINASSIGN5_I2C0_SCL_IO_SHIFT)| // Assign Pin 16 to SCL
  (10<<SWM_PINASSIGN5_I2C0_SDA_IO_SHIFT)); // Assign Pin 10 to SDA
  SYSCON->SYSAHBCLKCTRL0 &= ~(SYSCON_SYSAHBCLKCTRL0_SWM_MASK); // disable the switch matrix

  SYSCON->SYSAHBCLKCTRL0 |= SYSCON_SYSAHBCLKCTRL0_I2C0_MASK; // Enable I2C Clock
  SYSCON->PRESETCTRL0 |= SYSCON_PRESETCTRL0_I2C0_RST_N_MASK; // Clear I2C Reset

  SYSCON->FRG->FRGCLKSEL |= SYSCON_FRG_FRGCLKSEL_SEL(0); // Select fro (24 MHz) for frg clk

  // These MULT and DIV values make the frg clock equal 250 kHZ, frg0clk = frg0_src_clk/(1 + MULT/DIV).
  SYSCON->FRG->FRGMULT |= SYSCON_FRG_FRGMULT_MULT(190);
  SYSCON->FRG->FRGDIV |= SYSCON_FRG_FRGDIV_DIV(2);

  SYSCON->I2C0CLKSEL = 0b010; // Select frg0clk for I2C clock

  I2C0->MSTTIME |= I2C_MSTTIME_MSTSCLLOW(0); // Set low time to 2 cycles
  I2C0->MSTTIME |= I2C_MSTTIME_MSTSCLHIGH(0); // Set high time to 2 cycles

  I2C0->CFG |= I2C_CFG_MSTEN_MASK; // Configure I2C as master
}

// Performs the necessary initialization to use the USART
void usart_setup(){
  SYSCON->MAINCLKSEL = (0x0<<SYSCON_MAINCLKSEL_SEL_SHIFT);
  SYSCON->MAINCLKUEN &= ~(0x1);
  SYSCON->MAINCLKUEN |= 0x1;

  // Turn on clock, reset and remove reset
  SYSCON->SYSAHBCLKCTRL0 |= SYSCON_SYSAHBCLKCTRL0_UART0_MASK; 
  SYSCON->PRESETCTRL0 &= ~(SYSCON_PRESETCTRL0_UART0_RST_N_MASK);
  SYSCON->PRESETCTRL0 |= SYSCON_PRESETCTRL0_UART0_RST_N_MASK;

  // switch matrix
  SYSCON->SYSAHBCLKCTRL0 |= (SYSCON_SYSAHBCLKCTRL0_SWM_MASK);
  SWM0->PINASSIGN.PINASSIGN0 &= ~(SWM_PINASSIGN0_U0_TXD_O_MASK | SWM_PINASSIGN0_U0_RXD_I_MASK);
  SWM0-> PINASSIGN.PINASSIGN0 |= ( (0x4UL<<SWM_PINASSIGN0_U0_TXD_O_SHIFT) | // TXD is PIO0_4 into 7:0
  (0x0UL<<SWM_PINASSIGN0_U0_RXD_I_SHIFT)); 
  SYSCON->SYSAHBCLKCTRL0 &= ~(SYSCON_SYSAHBCLKCTRL0_SWM_MASK);

  // Setup clock
  SYSCON->UART0CLKSEL = 0x02;
  SYSCON->FRG[0].FRGCLKSEL = 0x00;
  SYSCON->FRG[0].FRGDIV = 0xFF;
  SYSCON->FRG[0].FRGMULT = 244;

  // Setup baud rate generator and oversample
  USART0->BRG = 1279; 
  USART0->OSR = 0xF;

  // Configure UART
  USART0->CFG |= ( 0x1<<USART_CFG_DATALEN_SHIFT |
  0x0<<USART_CFG_PARITYSEL_SHIFT |
  0x0<<USART_CFG_STOPLEN_SHIFT);
  USART0->CTL = 0;
  USART0->STAT = 0xFFFF;
  USART0->CFG |= USART_CFG_ENABLE_MASK;
}

// Sets up the gpio and iocon to use the pio0 pins
void gpio_setup(){
  SYSCON->SYSAHBCLKCTRL0 |= SYSCON_SYSAHBCLKCTRL0_GPIO0_MASK; // Enable GPIO clock
  SYSCON->SYSAHBCLKCTRL0 |= SYSCON_SYSAHBCLKCTRL0_IOCON_MASK; // Enable IOCON clock

  IOCON->PIO[IOCON_INDEX_PIO0_12] |= IOCON_PIO_MODE(2); // Pull up resistor enabled for PIO0_12 
  GPIO->DIR[0] |= 0 << 12; // Set PIO0_12 to input

  IOCON->PIO[IOCON_INDEX_PIO0_11] |= IOCON_PIO_MODE(2); // Pull up resistor enabled for PIO0_11 
  GPIO->DIR[0] |= 0 << 11; // Set PIO0_11 to input

  IOCON->PIO[IOCON_INDEX_PIO0_13] |= IOCON_PIO_MODE(2); // Pull up resistor enabled for PIO0_13
  GPIO->DIR[0] |= 0 << 13; // Set PIO0_13 to input

  SYSCON->SYSAHBCLKCTRL0 |= SYSCON_SYSAHBCLKCTRL0_IOCON(0); // Disable IOCON clock to conserve power
}

void read_temp(float *data){
	uint8_t LM75_Buf[2];

	WaitI2CPrimaryState(I2C0, I2C_STAT_MSTST_IDLE);

  // Start transmition in write mode
	I2C0->MSTDAT = (LM75_I2CAddress<<1) | 0;
	I2C0->MSTCTL = MSTCTL_START;

	WaitI2CPrimaryState(I2C0, I2C_STAT_MSTST_TXRDY); 

  // Write a 0 to the lm75 pointer register (0 means temp)
	I2C0->MSTDAT = 0x00;
	I2C0->MSTCTL = MSTCTL_CONTINUE;

	WaitI2CPrimaryState(I2C0, I2C_STAT_MSTST_TXRDY);

  // Start transmission in read mode
	I2C0->MSTDAT = (LM75_I2CAddress<<1) | 1;
	I2C0->MSTCTL = MSTCTL_START;

  WaitI2CPrimaryState(I2C0, I2C_STAT_MSTST_RXRDY);

  // Read first byte
	LM75_Buf[0] = I2C0->MSTDAT;
	I2C0->MSTCTL = MSTCTL_CONTINUE;

	WaitI2CPrimaryState(I2C0, I2C_STAT_MSTST_RXRDY);

  // Read second byte
	LM75_Buf[1] = I2C0->MSTDAT;
	I2C0->MSTCTL = MSTCTL_STOP;

  WaitI2CPrimaryState(I2C0, I2C_STAT_MSTST_IDLE);

  // Convert buffer to usable float
  // - LM75_Buf[0] & LM75_Buf[1] are MSB and LSB respectively
  // - Value in twos compliment form
  // - Last 5 digits are garbage
  // - Resolution of 0.125
	if( (LM75_Buf[0]&0x80) == 0x00){ // If msb is 0, implying positive number
		*data = ((float) (((LM75_Buf[0]<<8) + LM75_Buf[1])>>5)) * 0.125; 
	}
	else{ // If mab is 1, implying negative number
		*data = 0x800 - (( LM75_Buf[0]<<8) + ( LM75_Buf[1]>>5));
		*data = -(((float)(*data)) * 0.125);
	}
}

// Helper function used for i2c communication
void WaitI2CPrimaryState(I2C_Type * ptr_LPC_I2C, uint32_t state) {

	// Check the Primary Pending bit (bit 0 of the i2c stat register)
	while(!(ptr_LPC_I2C->STAT & I2C_STAT_MSTPENDING_MASK)); // Wait
	if((ptr_LPC_I2C->STAT & PRIMARY_STATE_MASK) != state) // If primary state mismatch
	{
	  while(1); // die here and debug the problem
	}
	return; 
}

void checkButtonPressed(int button, bool *button_pressed){
  char button_str[3];
  
  // Button is in pressed state, and was not previously in pressed state (i.e button was pressed)
  if ((*button_pressed == false) && (GPIO->B[0][button] == 0)) {
    *button_pressed = true; // Set button_pressed state to true
  }

  // Button is in not pressed state, and was previously in pressed (i.e button was released)
  else if ((*button_pressed == true) && (GPIO->B[0][button] == 1)) {
    switch (button) {
      case BUTTON1:
        sprintf(button_str, "B%i\n", 1);
        break;
      case BUTTON2:
        sprintf(button_str, "B%i\n", 2);
        break;
      case BUTTON3:
        sprintf(button_str, "B%i\n", 3);
        break;
    }
    USART_WriteBlocking(USART0, button_str, sizeof(button_str));
    *button_pressed = false;
  }
}


int main(void) {

  __disable_irq(); // turn off globally

  i2c_setup();
  usart_setup();
  gpio_setup();

  float *temp = malloc(sizeof(float));
  int temp_scaled; // Scales the temp by 1000 (due to 3 point precision) into an int. Embedded C has limited functionality with floats
  char temp_str[7];

  bool button1_pressed = false;
  bool button2_pressed = false;
  bool button3_pressed = false;

  while(1) {
    read_temp(temp);
    temp_scaled = (int) ((*temp)*1000);
    sprintf(temp_str, "T%i\n", temp_scaled);
    USART_WriteBlocking(USART0, temp_str, sizeof(temp_str));
    checkButtonPressed(BUTTON1, &button1_pressed);
    checkButtonPressed(BUTTON2, &button2_pressed);
    checkButtonPressed(BUTTON3, &button3_pressed);
    asm("NOP");
  }
}
