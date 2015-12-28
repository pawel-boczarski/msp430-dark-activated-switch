// Base sample:
//******************************************************************************
//  MSP430G2x31 Demo - ADC10, DTC Sample A1 32x, AVcc, Repeat Single, DCO
//
//  Description: Use DTC to sample A1 32 times with reference to AVcc. Software
//  writes to ADC10SC to trigger sample burst. In Mainloop MSP430 waits in LPM0
//  to save power until ADC10 conversion burst complete, ADC10_ISR(DTC) will
//  force exit from any LPMx in Mainloop on reti. ADC10 internal oscillator
//  times sample period (16x) and conversion (13x). DTC transfers conversion
//  code to RAM 200h - 240h. P1.0 set at start of conversion burst, reset on
//  completion.
//  D. Dang
//  Texas Instruments Inc.
//  October 2010
//  Built with CCS Version 4.2.0 and IAR Embedded Workbench Version: 5.10
/********************************************************************************/

// Actual project:

// ===> 1 Jun 2012
// a light detector exploiting a photodiode with the cathode on VCC
// and the anode connected GND through a 4k3 resistor, anode voltage sampled at A1 input.
// A diode is lit when light amount below certain threshold.
// P. Boczarski
// ===> 5 Jun 2012
// single conversion
// P. Boczarski
// ===> whole June - lots of changes. When darkness falls, a sequence is fired to gradually dim the led.
// The clock is XTCLK1 most of time, but for light dimming sequence it is switched to DCOCLK to provide smooth interrupt service.
// P. Boczarski
// ===> 9 Jul 2012
// Three diodes added at P1.0, P1.4, P1.7, therefore some code changes.
// P. Boczarski
//
// todo => LPM3
// one conversion a second
//******************************************************************************


//          (ADC input)
//              P1.1
//                |     ~ 4 kOhm
//                |    (forms a voltage divider
//                |     with photodiode together)
// VCC ---|<|-----+------[====]------------------ GND
//     photodiode
//   (reverse-biased!)


//                    limiting resistor
//                   (330 Ohm - 1 kOhm)
// P1.0  ----|<|-------------[====]--------- GND

//                    limiting resistor
//                   (330 Ohm - 1 kOhm)
// P1.4  ----|<|-------------[====]--------- GND

//                    limiting resistor
//                   (330 Ohm - 1 kOhm)
// P1.7  ----|<|-------------[====]--------- GND

#include  <msp430g2231.h>
#undef MSP430_GCC

// 0x0a0 is fine in most conditions
#define DARKNESS_THRESHOLD_L	(0x090)
#define DARKNESS_THRESHOLD_H	(0x0a0)
// warning: will be rounded to even number
#define LIGHTUP_TIME_SEC		30

// must be 100 * N !
#define DUTY_CYCLE_TICKS		1000
#define DUTY_CYCLE_PRESC					(DUTY_CYCLE_TICKS / 100)

// With DCO 1MHz timer clock, we have a timer overflow 1000000 / 65536 = 15 times a second
#define LIGHTUP_TIME_INT		(LIGHTUP_TIME_SEC * 15)

// the ambient light state
enum { LIGHT, DARK, SEQUENCE } currentState = LIGHT;

// time since the diodes activation
unsigned int time_since_activation = 0xFFFF;

enum { PWM_OFF, PWM_ON } pwmState = PWM_OFF;
int duty;

// Here we have P1.0, P1.4, P1.7 connected.
// Please modify this you have more diodes connected.
#define LEDS_MASK	(0x91)

#define LIGHT_LEDS()	do {		\
							P1OUT |= LEDS_MASK; \
						} while(0)

#define DIM_LEDS()		do {		\
							P1OUT &= ~LEDS_MASK; \
						} while(0)

#define SPEED_UP_MCLK()		do {		\
					BCSCTL2 &= ~SELM_3;	\
					} while(0)

#define SLOW_DOWN_MCLK()		do {		\
					BCSCTL2 |= SELM_3;	\
					} while(0)



void main(void)
{
  WDTCTL = WDTPW + WDTHOLD;                 // Stop WDT

  BCSCTL2 |= SELM_3;								// select VLOCLK / LFXT1CLK for MCLK, DCOCLK for SMCLK
  // With external quartz, MCLK = 32kHz, SMCLK = 1MHz

  ADC10CTL1 = CONSEQ_0 + INCH_1 + ADC10SSEL_1;     // Single channel, single conversion, channel A1, ACLK
  ADC10CTL0 = ADC10SHT_2 + ADC10ON + ADC10IE;	   // ADC10ON, interrupt enable

  ADC10AE0 |= 0x02;                         // P1.1 ADC option select

  P1DIR |= LEDS_MASK;
  CCTL0 = CCIE;
  CCTL1 = CCIE;                             // CCR1 interrupt enabled

  TACTL = TASSEL_2 + MC_2 + TAIE;                  // SMCLK, continuous

  for (;;)
  {
    _BIS_SR(CPUOFF + GIE);                 // Enter LPM3 w/ interrupt
  }
}

void _setDutyOnP16 (int percent) {
	if(percent * DUTY_CYCLE_PRESC < 20) {
		percent = 0;		// the lightup period to short to service two interrupts
	}

	if(percent == 0) {
		if(pwmState == PWM_ON) {
			pwmState = PWM_OFF;
			SLOW_DOWN_MCLK();
		}
		DIM_LEDS();
	} else if(percent == 100) {
		if(pwmState == PWM_ON) {
			pwmState = PWM_OFF;
			SLOW_DOWN_MCLK();
		}
		LIGHT_LEDS();
	} else {
		//_ccr1_preset = percent * DUTY_CYCLE_PRESC;
		if(pwmState == PWM_OFF) {
			SPEED_UP_MCLK();
			LIGHT_LEDS();
			pwmState = PWM_ON;
		}
		TACCR0 = TAR + percent * DUTY_CYCLE_PRESC;
		TACCR1 = TAR + DUTY_CYCLE_TICKS;
		duty = percent;
	}
}

// ADC10 interrupt service routine
#ifdef MSP430_GCC
interrupt(ADC10_VECTOR) ADC10_ISR(void)
#else
#pragma vector=ADC10_VECTOR
void __attribute__((interrupt(ADC10_VECTOR))) ADC10_ISR(void)
#endif
{
	if(ADC10MEM < DARKNESS_THRESHOLD_L) {
		switch(currentState) {
		case DARK:
			// it's still dark, nothing to do.
		break;
		case LIGHT:
		{
			// darkness fell, begin the sequence!
			time_since_activation = 0;
			currentState = SEQUENCE;
		}
		break;
		case SEQUENCE:
			// the light dimming in progress,
			// state will be changed to DARK in
			// Timer interrupt.
		break;
		}

	} else if(ADC10MEM > DARKNESS_THRESHOLD_H) {

		switch(currentState) {
		case DARK:
		{
			// it was dark, but now light has come.
			// no action with diodes is needed.
			time_since_activation = 0xFFFF;
			currentState = LIGHT;
		}
		break;
		case LIGHT:
			// there is still light, no need to do anything
		break;
		case SEQUENCE:
		{
			// it was dark, but now light has come.
			// next ADC conversion finished interrupt will
			// put the diodes off.
			time_since_activation = 0xFFFF;
			currentState = LIGHT;
		}
		break;
		}
	}

  __bic_SR_register_on_exit(CPUOFF);        // Clear CPUOFF bit from 0(SR)
}

// only CCR0 sources this interrupt
#ifdef MSP430_GCC
interrupt(TIMERA0_VECTOR) TIMERA0_ISR(void)
#else
#pragma vector=TIMERA0_VECTOR
__interrupt void Timer_A0()
#endif
{
	if(pwmState == PWM_ON) {
		DIM_LEDS();
		CCR0 += DUTY_CYCLE_TICKS;
	}

    __bic_SR_register_on_exit(CPUOFF);        // Clear CPUOFF bit from 0(SR)
}


#ifdef MSP430_GCC
interrupt(TIMERA1_VECTOR) TIMERA1_ISR(void)
#else
#pragma vector=TIMERA1_VECTOR
void __attribute__((interrupt(TIMERA1_VECTOR))) TIMERA1_ISR(void)
#endif
{

	switch(TAIV) {
	case TAIV_TAIFG:							// timer overflow
	{

		ADC10CTL0 |= ENC + ADC10SC;             // Sampling and conversion start

		switch(currentState) {
		case LIGHT:
		{
			_setDutyOnP16(0);
		}
		break;
		case DARK:
		{
			// timer only works in "sequence" state
			_setDutyOnP16(0);
		}
		break;
		case SEQUENCE:
		{
			if(time_since_activation <= LIGHTUP_TIME_INT) {
				_setDutyOnP16(100 - (100*time_since_activation) / LIGHTUP_TIME_INT);
			}
			else {
				_setDutyOnP16(0);
				time_since_activation = 0xFFFF;
				currentState = DARK;
			}
		}
		break;
		}

		// should be inc only when time_since_activation != 0xFFFF
		if(time_since_activation != 0xFFFF) {
			time_since_activation++;
		}
	}
	break;

	case TAIV_TACCR1:		// CCR1
	{
		if(pwmState == PWM_ON) {
			LIGHT_LEDS();
			CCR1 += DUTY_CYCLE_TICKS;
		}
	}
	break;

	}

    __bic_SR_register_on_exit(CPUOFF);        // Clear CPUOFF bit from 0(SR)
}
