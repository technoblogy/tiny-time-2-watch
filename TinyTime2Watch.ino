/* Tiny Time 2 Watch v2 - see http://www.technoblogy.com/show?1MG3

   David Johnson-Davies - www.technoblogy.com - 30th May 2019
   ATtiny85 @ 8 MHz (internal oscillator; BOD disabled)
   
   CC BY 4.0
   Licensed under a Creative Commons Attribution 4.0 International license: 
   http://creativecommons.org/licenses/by/4.0/
*/

#include <avr/sleep.h>
#include <avr/power.h>

const int Tickspersec = 250;              // Timer/Counter0

volatile uint8_t Timeout;
volatile int Offset = 0;                  // For setting time
volatile byte Cycle = 0;

volatile int Hours = 0;                   // From 0 to 11, or 12 = off.
volatile int Fivemins = 0;                // From 0 to 11, or 12 = off.

// Pin assignments

int Pins[5][5] = {{ -1,  1,  3, -1,  5 },
                  {  2, -1, 10, -1,  0 },
                  {  9, 11, -1, -1,  7 },
                  { -1, -1, -1, -1, -1 },
                  {  4,  6,  8, -1, -1 } };

// Display multiplexer **********************************************

void DisplaySetup () {
  // Set up Timer/Counter0 to multiplex the display
  TCCR0A = 2<<WGM00;            // CTC mode; count up to OCR0A
  TCCR0B = 0<<WGM02 | 4<<CS00;  // Divide by 256
  OCR0A = 124;                  // Divide by 125 -> 250Hz
}

void DisplayOn () {
  TIMSK = 1<<OCIE0A;    // Enable compare match interrupt
}

void DisplayOff () {
  TIMSK = 0;            // Disable compare match interrupt
  DDRB = 0;             // Blank display - all inputs
  PORTB = 0x17;         // All pullups on except PB3
}
  
void DisplayNextRow() {
  Cycle++;
  byte row = Cycle & 0x03;
  if (row == 3) row = 4;    // Skip PB3
  byte bits = 0;
  for (int i=0; i<5; i++) {
    if (Hours == Pins[row][i]) bits = bits | 1<<i;
    if (Fivemins == Pins[row][i]) bits = bits | 1<<i;
  }
  DDRB = 1<<row | bits;
  PORTB = bits;
}

// Timer/Counter0 interrupt - multiplexes display and counts ticks
ISR(TIM0_COMPA_vect) {
  DisplayNextRow();
  Timeout--;
  Offset++;
}

// Delay in 1/250 of a second
void Delay (int count) {
  Timeout = count;
  while (Timeout);
}

// One Wire Protocol **********************************************

// Buffer to read data or ROM code
static union {
  uint8_t DataBytes[5];
  struct {
    uint8_t control;
    long seconds;
  } rtc;
};

const int OneWirePin = 3;

const int SkipROM = 0xCC;
const int WriteClock = 0x99;
const int ReadClock = 0x66;

inline void PinLow () {
  DDRB = DDRB | 1<<OneWirePin;
}

inline void PinRelease () {
  DDRB = DDRB & ~(1<<OneWirePin);
}

// Returns 0 or 1
inline uint8_t PinRead () {
  return PINB>>OneWirePin & 1;
}

void DelayMicros (int micro) {
  TCNT1 = 0; TIFR = 1<<OCF1A;
  OCR1A = (micro>>1) - 1;
  while ((TIFR & 1<<OCF1A) == 0);
}

void LowRelease (int low, int high) {
  PinLow();
  DelayMicros(low);
  PinRelease();
  DelayMicros(high);
}

uint8_t OneWireSetup () {
  TCCR1 = 0<<CTC1 | 0<<PWM1A | 5<<CS10;  // CTC mode, 500kHz clock
  GTCCR = 0<<PWM1B;
 }

uint8_t OneWireReset () {
  uint8_t data = 1;
  LowRelease(480, 70);
  data = PinRead();
  DelayMicros(410);
  return data;                           // 0 = device present
}

void OneWireWrite (uint8_t data) {
  int del;
  for (int i = 0; i<8; i++) {
    if ((data & 1) == 1) del = 6; else del = 60;
    LowRelease(del, 70 - del);
    data = data >> 1;
  }
}

uint8_t OneWireRead () {
  uint8_t data = 0;
  for (int i = 0; i<8; i++) {
    LowRelease(6, 9);
    data = data | PinRead()<<i;
    DelayMicros(55);
  }
  return data;
}

// Read bytes into array, least significant byte first
void OneWireReadBytes (int bytes) {
  for (int i=0; i<bytes; i++) {
    DataBytes[i] = OneWireRead();
  }
}

// Write bytes from array, least significant byte first
void OneWireWriteBytes (int bytes) {
  for (int i=0; i<bytes; i++) {
     OneWireWrite(DataBytes[i]);
  }
}

// Setup **********************************************

void SetTime () {
  unsigned long secs = 0;
  for (;;) {
    Fivemins = (unsigned long)(secs/300)%12;
    Hours = (unsigned long)((secs+1799)/3600)%12;
    // Write time to RTC
    rtc.control = 0x0C;
    rtc.seconds = secs + (Offset/Tickspersec);
    OneWireReset();
    OneWireWrite(SkipROM);
    OneWireWrite(WriteClock);
    OneWireWriteBytes(5);
    DisplayOn();
    Delay(Tickspersec);
    DisplayOff();
    secs = secs + 300;
  }
}

void setup () {
  OneWireSetup();
  DisplaySetup();
  // Disable what we don't need to save power
  ADCSRA &= ~(1<<ADEN);         // Disable ADC
  PRR = 1<<PRUSI | 1<<PRADC;    // Turn off clocks
  set_sleep_mode(SLEEP_MODE_PWR_DOWN);
  // Set time on a power-on reset
  if (MCUSR & 1<<PORF) { MCUSR = 0; SetTime(); }
}

void loop () {
  unsigned long secs;
  // First read the time
  OneWireReset();
  OneWireWrite(SkipROM);
  OneWireWrite(ReadClock);
  OneWireReadBytes(5);
  OneWireReset();
  secs = rtc.seconds;
  //
  // Then display it  
  Hours = (unsigned long)((secs+1800)/3600)%12;
  Fivemins = 12;
  int Mins = (unsigned long)(secs/60)%60;
  int From = Mins/5;
  int Count = Mins%5;
  DisplayOn();
  for (int i=0; i<5-Count; i++) {
    Fivemins = From; Delay(Tickspersec/5);
    Fivemins = 12; Delay(Tickspersec/5);
  }
  for (int i=0; i<Count; i++) {
    Fivemins = (1+From)%12; Delay(Tickspersec/5);
    Fivemins = 12; Delay(Tickspersec/5);
  }
  DisplayOff();
  sleep_enable();
  sleep_cpu();
}
