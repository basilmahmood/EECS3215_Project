#include <stdio.h>
#include "LPC802.h"
#include "fsl_i2c.h"
#include "fsl_usart.h"
// Defines

#define LED_USER1 (8)// use with WKT
#define LED_USER2 (9)// use with SysTick
#define LPC_I2C0BUFFERSize (35)
#define LPC_I2C0BAUDRate (100000)// 100kHz

volatile uint8_t g_I2C0DataBuf[LPC_I2C0BUFFERSize];
volatile uint8_t g_I2C0DataCnt;

#define WKT_FREQ 1000000 // Use if the WKT is clocked by the LPOSC
#define WKT_RELOAD 100000 // Reload value for the WKT down counter

uint32_t g_WKT_RELOAD = WKT_RELOAD; // counter reload value for WKT

#define LM75_I2CAddress (0x4C)

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
    // ----------------------- Begin Core Clock Select -----------------------
  // Specify that we will use the Free-Running Oscillator
  // Set the main clock to be the FRO
  // 0x0 is FRO; 0x1 is external clock ; 0x2 is Low pwr osc.; 0x3 is FRO_DIV
  // Place in bits 1:0 of MAINCLKSEL.
  SYSCON->MAINCLKSEL = (0x0<<SYSCON_MAINCLKSEL_SEL_SHIFT);
  // Update the Main Clock
  // Step 1. write 0 to bit 0 of this register
  // Step 2. write 1 to bit 0 this register
  SYSCON->MAINCLKUEN &= ~(0x1);// step 1. (Sec. 6.6.4 of manual)
  SYSCON->MAINCLKUEN |= 0x1;// step 2. (Sec. 6.6.4 of manual)
  // Set the FRO frequency (clock_config.h in SDK)
  //
  // For FRO at 9MHz: BOARD_BootClockFRO18M();
  // 12MHz: BOARD_BootClockFRO24M();
  // 15MHz: BOARD_BootClockFRO30M();
  // See: Section 7.4 User Manual
  // This is more complete than just using set_fro_frequency(24000); for 12 MHz
  BOARD_BootClockFRO24M(); // 30M, 24M or 18M for 15MHz, 12MHz or 9MHz
  // ----------------------- End of Core Clock Select -----------------------

  // -------------- Step 1: Turn on the USART0 ---------------------
  // Bit is 14 (USART0). Set to 1 to turn on USART0.
  // Optionally, we could also turn on USART1 with bit 15
  SYSCON->SYSAHBCLKCTRL0 |= (SYSCON_SYSAHBCLKCTRL0_UART0_MASK); // Set bit 14.
  // -------------- Step 2: Reset the USART0 ---------------------
  // Set bit 14 to 0: assert (i.e. "make") the USART0 reset
  // Set bit 14 to 1: remove the USART0 reset
  SYSCON->PRESETCTRL0 &= ~(SYSCON_PRESETCTRL0_UART0_RST_N_MASK); // Reset USART0
  SYSCON->PRESETCTRL0 |= (SYSCON_PRESETCTRL0_UART0_RST_N_MASK); // remove the reset.

  // enable switch matrix
  SYSCON->SYSAHBCLKCTRL0 |= (SYSCON_SYSAHBCLKCTRL0_SWM_MASK);
  // Set switch matrix
  // Clear bits 15:0
  SWM0->PINASSIGN.PINASSIGN0 &= ~(SWM_PINASSIGN0_U0_TXD_O_MASK | SWM_PINASSIGN0_U0_RXD_I_MASK);
  // Assign TXD and RXD ports to PINASSIGN0 bits 15:0
  // TXD is PIO0_4, so put 4 into bits 7:0
  // RXD is PIO0_0, so put 0 into bits 15:8
  SWM0-> PINASSIGN.PINASSIGN0 |= ( (0x4UL<<SWM_PINASSIGN0_U0_TXD_O_SHIFT) | // TXD is PIO0_4 into 7:0
  (0x0UL<<SWM_PINASSIGN0_U0_RXD_I_SHIFT)); // RXD is PIO0_0
  // USART0 is now set to PIO0_4 for TX and PIO0_0 for RX.
  // disable the switch matrix
  SYSCON->SYSAHBCLKCTRL0 &= ~(SYSCON_SYSAHBCLKCTRL0_SWM_MASK);
  // ---------------- End of Switch Matrix code -----------------------------------

  // -------------- Step 4: Configure USART0 Clock ---------------------
  SYSCON->UART0CLKSEL = 0x02;
  SYSCON->FRG[0].FRGCLKSEL = 0x00;
  SYSCON->FRG[0].FRGDIV = 0xFF;
  SYSCON->FRG[0].FRGMULT = 244;

  USART0->BRG = 1279; 
  USART0->OSR = 0xF;

  // USART CONFIG (only one bit to set here, really: data length)
  USART0->CFG |= ( 0x1<<USART_CFG_DATALEN_SHIFT | // Data length: 8
  0x0<<USART_CFG_PARITYSEL_SHIFT | // Parity: none
  0x0<<USART_CFG_STOPLEN_SHIFT); // 1 stop bit.
  // nothing to be done here.
  USART0->CTL = 0;
  // clear any pending flags, just in case something happened.
  USART0->STAT = 0xFFFF;
  // Enable the USART0
  USART0->CFG |= USART_CFG_ENABLE_MASK;// set bit 1 to 1.
}

void lm75_read(float *data){
	uint8_t LM75_Buf[2];

	WaitI2CPrimaryState(I2C0, I2C_STAT_MSTST_IDLE); // Wait for the Primary's state to be idle
	I2C0->MSTDAT = (LM75_I2CAddress<<1) | 0; // Address with 0 for RWn bit (WRITE)
	I2C0->MSTCTL = MSTCTL_START; // Start the transaction by setting the

	// MSTSTART bit to 1 in the Primary control register
	WaitI2CPrimaryState(I2C0, I2C_STAT_MSTST_TXRDY); // Wait for the address to be ACK'd
	I2C0->MSTDAT = 0x00;
	I2C0->MSTCTL = MSTCTL_CONTINUE;

	WaitI2CPrimaryState(I2C0, I2C_STAT_MSTST_TXRDY); // Wait for the address to be ACK'd
	I2C0->MSTDAT = (LM75_I2CAddress<<1) | 1; // Address with 1 for RWn bit (WRITE)
	I2C0->MSTCTL = MSTCTL_START; // Start the transaction by setting the

	// MSTSTART bit to 1 in the Primary control register.

	// PRIMARY_STATE_MASK is (0x7<<1)
	while ((I2C0->STAT & PRIMARY_STATE_MASK) != I2C_STAT_MSTST_RXRDY)
	{
		// just spin... (wait)
	}

	LM75_Buf[0] = I2C0->MSTDAT;
	I2C0->MSTCTL = MSTCTL_CONTINUE;
	// Wait for the address to be ACK’d
	WaitI2CPrimaryState(I2C0, I2C_STAT_MSTST_RXRDY);
	// PRIMARY_STATE_MASK is (0x7<<1)

	while ((I2C0->STAT & PRIMARY_STATE_MASK) != I2C_STAT_MSTST_RXRDY)
	{
		// just spin... (wait)
	}

	LM75_Buf[1] = I2C0->MSTDAT;
	I2C0->MSTCTL = MSTCTL_STOP;

	// PRIMARY_STATE_MASK is (0x7<<1)
	while ((I2C0->STAT & PRIMARY_STATE_MASK) != I2C_STAT_MSTST_IDLE)
	{
		// just spin...
	}

	// Convert the buffer into a single variable
	// accessible outside this function via the *data pointer.
	if( (LM75_Buf[0]&0x80) == 0x00){
		*data = ((float)( ((LM75_Buf[0]<<8) + LM75_Buf[1])>>5) * 0.125);
	}
	else{
		*data = 0x800 - (( LM75_Buf[0]<<8) + ( LM75_Buf[1]>>5) );
		*data = -(((float)(*data)) * 0.125);
	}
}

void WaitI2CPrimaryState(I2C_Type * ptr_LPC_I2C, uint32_t state) {

	// Check the Primary Pending bit (bit 0 of the i2c stat register)
	// Wait for MSTPENDING bit set in STAT register
	while(!(ptr_LPC_I2C->STAT & I2C_STAT_MSTPENDING_MASK)); // Wait
	// Check to see that the state is correct.
	// if it is not, then turn on PIO0_9 to indicate a problem
	// Primary's state is in bits 3:1. PRIMARY_STATE_MASK is (0x7<<1)
	if((ptr_LPC_I2C->STAT & PRIMARY_STATE_MASK) != state) // If primary state mismatch
	{
	GPIO->DIRCLR[0] = (1UL<<LED_USER2); // turn on LED on PIO0_9 (LED_USER2)
	while(1); // die here and debug the problem
	}
	return; // If no mismatch, return
	}


int main(void) {

  __disable_irq(); // turn off globally

  i2c_setup();
  usart_setup();

  float *temp = malloc(sizeof(float));
  int temp_scaled; // Scales the temp by 1000 (due to 3 point precision) into an int. Embedded C has limited functionality with floats
  char temp_str[6];

  while(1) {
    lm75_read(temp);
    temp_scaled = (int) ((*temp)*1000);
    sprintf(temp_str, "%i\n", temp_scaled);
    USART_WriteBlocking(USART0, temp_str, sizeof(temp_str));
    asm("NOP");
  }
}
