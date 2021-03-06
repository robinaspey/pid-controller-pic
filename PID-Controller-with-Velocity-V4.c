﻿//*******************************************************************
//   Program:    PID-Controller-with-Velocity-V4.c
//   Author:     R.Aspey
//   Compiler:   CCS Version 4.038
//
// This program is for controlling a single PID loop with an AD7705 
// as input with scaling from 0 to 10V.
// The user setup for PID and other setup data are saved to EEPROM. 
// It also facilitates the PID control algorithm. Control Loop Tuning 
// Using either manual or Ziegler - Nichols Tuning. Kp is the gain in 
// units of proportional band - O/P voltage is proportional to error 
// magnitude. Ki is proportional the sum of the previous error terms 
// Kd is proportional to the rate of change of measurement error.
// F(t)=(Kp. )+(Ki. )+(Kd. )
// Note 1: Will modify to use Hungarian notation at a later date for 
// documentation purposes. (19/10/2010)
// Note 2: Added save for setup.
// Note 3: Calibration Structure Added for Pressue - this may later 
// contain coefficients to linearize the sensor output.
// Note 4: Added code to process command strings from PC.
// Note 5: Added restart causes
// Note 6: Complete (requires full soak testing)
// Note 7: Added timer code for CCP1 and CCP2 for velocity encoder 
//         feedback and conversion to RPM.
// Note 8: Added code to test conversion of bits on DAC
// Note 9: Added extra write to DAC as sometimes it didnt work ?
//*******************************************************************
#include   <18F4620.h>
#device    PASS_STRINGS=IN_RAM
#device     *= 16;
#fuses     HS, WDT, WDT256, NOPROTECT, BROWNOUT, PUT, NOLVP, NODEBUG, MCLR
#use       DELAY(CLOCK=20MHz, RESTART_WDT)
           // Modified so it restarts WDT./
#use       RS232(BAUD=115200, XMIT=PIN_C6, RCV=PIN_C7, ERRORS, STREAM=USB,
           RESTART_WDT, BITS=8, PARITY=N, STOP=1)
#use       R232(BAUD=19200 , XMIT=PIN_C0, ERRORS, STREAM=USB1, RESTART_WDT,
           BITS=8, PARITY=N, STOP=1)
#use       SPI(MASTER, DO=PIN_C5, DI=PIN_C4, CLK=PIN_C3, MSB_FIRST, MODE=0,
           BITS=16, STREAM=SPI)

#include <string.h> 
#include <stdlib.h> 
#include <float.h>

#define    VERBOSE          1
#define    SILENT           0
#define    ERASE            1
#define    NO_ERASE         0
#define    SETUP_PRESENT    0x62
#define    FIFO_SIZE        12

#define    LED_STATUS PIN_D0
#define    ADC_RESET  PIN_D1
#define    ADC_DRDY   PIN_D2
// Pin D3 
// Pin D4
#define    AD7705_CS  PIN_D5
#define    AD7243_CS  PIN_D6
// Pin D7 Space for MAX186 Chip Sel

#define    RS232_TXD  PIN_C0
#define    CCP1_IN    PIN_C1
#define    CPP2_IN    PIN_C2
#define    ADC_CLK    PIN_C3
#define    ADC_DI     PIN_C4
#define    ADC_D0     PIN_C5
#define    TXD        PIN_C6
#define    RXD        PIN_C7

// AD7705 Calibration Mode Settings
#define    ADC_NORMAL      0x00
#define    ADC_SELF        0x40
#define    ADC_ZERO_SCALE  0x80
#define    ADC_FULL_SCALE  0xC0

// AD7705 Gain Settings
#define    ADC_GAIN_1      0x00
#define    ADC_GAIN_2      0x08
#define    ADC_GAIN_4      0x10
#define    ADC_GAIN_8      0x18
#define    ADC_GAIN_16     0x20
#define    ADC_GAIN_32     0x28
#define    ADC_GAIN_64     0x30
#define    ADC_GAIN_128    0x38

// AD7705 Polar Operations
#define    ADC_BIPOLAR     0x04
#define    ADC_UNIPOLAR    0x00
#define    ADC_CORRECT     0x04

// AD7705 Update rates
#define    ADC_50          0x04
#define    ADC_60          0x05
#define    ADC_250         0x06
#define    ADC_500         0x07

typedef    unsigned char   BOOL ;
typedef    unsigned char   UINT8 ;
typedef    unsigned int16  UINT16;
typedef    unsigned int32  UINT32;

//    Note: Ammend this at compile
const char DEFAULT_STRING[]={"CH1 Pressure "}; 
const char BLANK_STRING[]={"No Setup"};

//    void  get_PID(void);
void setup_ad7705(int, int, int, int, int, int); 
void read_eeprom_string( char *, int8, int8); 
void write_eeprom_string(char *, int8, int8); 
void get_string(char*, int);
void write_adc_byte(BYTE data); 
void load_setup_from_nvm(void); 
void init_setup_defaults(void); 
void save_setup_to_nvm(void);
void get_restart_cause(void); 
void clear_structure(void); 
UINT16 read_zero_scale(int1);

UINT8 sscanf(char *, char *, char *);
UINT8 state_machine(void); 
void check_erased(void); 
void exercise_dac(void); 
void exercise_adc(int1); 
void show_values(void); 
void erase_nvm(int16);
void save_setup_to_nvm(int8); 
void menu(void);

float get_dac_volts(float mv, float sp, float pb); 
float get_mpa(int16);
float absolutef(float, float);
int16 absolute(int16, int16);

BYTE get_state(void);
char g_istream[FIFO_SIZE];

void run_pid(void);
char timed_getc(long);
long int read_adc_value(int1); 
long int read_adc_word(void);

void adc_setup_device(int, int, int, int);
void adc_disable(void); 
void write_dac(UINT16); 
void init_ad7705(int1);

UINT16 get_adc_filtered(int1, int8); 
UINT16 absolute(int16, int16);
UINT16 get_valid_adc_data(int1); 
UINT16 get_dac_bits(float);
UINT16 s_size;

unsigned long rise; 
unsigned long fall; 
unsigned long pulse_width;

#INT_CCP2 
void isr()
{    rise =     CCP_1;
     fall =     CCP_2;
     
     pulse_width = fall - rise;
}

// int16   write_motor(float);
BYTE rx_byte;

/*#INT_RDA
void  process_commands(void)
{
LONG  timeout=0xfff;
UINT8 i=0, ch;

setup_wdt(WDT_ON);
do   {     timeout = 0xfff;
           ch = timed_getc(50000);
           g_istream[i++] = ch;
           // This is a holding buffer
           #asm
                CLRWDT
           #endasm
           if (timeout--) break;
     }     while(i < FIFO_SIZE && ch != 0x0a);
           rx_byte = TRUE;
           fprintf(USB, "\r\nInterrupt! %s", &g_istream[0]);
} */

struct SENSOR
{    int16 LV_BITS;        // This is the ADC value at 0 MPa
     int16 HV_BITS;        // This is the max ADC value at HV_MPa 
     float MAX_MPA ;
     float coefs[3];       // Calibration / linearization coefficients
}    cal ;

struct PID
{    char ident[sizeof(DEFAULT_STRING)]    ;
     float Kp, Ki, Kd ;    // Proportional, Integral and Derivative Gain
     float rate       ;    // This is the ramp rate in KPa/min
     float rsp        ;    // This is the setpoint for ramping in MPa.
     float tsp        ;    // This is the target setpoint in MPa.
     float mv         ;    // On startup this will be read as zero
     float pb         ;    // Proportional band in KPa.
     float mverr      ;    // MV Error signal value - not stored.
     BOOL fwd[4]      ;    // Is loop forward or reverse PID
     BOOL setup_ok    ;    // This value should be 0x62

//   struct SENSOR cal ;
}    trx, *ptrx
;    // declaration of structure and pointer to an instance of this

char buffer[sizeof(trx)];

//******************************************************************* 
//   Declaration of globals functions
//*******************************************************************
void init_pulse_width_counter(void)
{
setup_ccp1(CCP_CAPTURE_RE); 
setup_ccp2(CCP_CAPTURE_FE); 
setup_timer_1(T1_INTERNAL); 
enable_interrupts(GLOBAL); 
setup_wdt(WDT_ON);
}

void enable_pulse_width_counter(void)
{
enable_interrupts(INT_CCP2);
}

UINT16 get_CCP2_period(void)
{
UINT16 period;
period = 2*(pulse_width/5);
return(period);
}

void disable_pulse_width_counter(void)
{
disable_interrupts(INT_CCP2);
}

float get_motor_rpm(int1)
{
#define   SF   1024 // There are 1024 pulses / rev
                    // therefore gives 1024Hz = 1RPS.
UINT16 i=50;
float32   period, freq, rpm;

init_pulse_width_counter(); 
enable_pulse_width_counter(); 
delay_ms(50);

period = (float32)get_CCP2_period();
freq = 1000000L /period;
rpm  = (freq / 1024)*60;
disable_pulse_width_counter(); 
return(rpm);
}

void main(void) 
{
BYTE ch = 0;
int16 timeout=15; 
int8 state;
ptrx =    &trx;
s_size =  sizeof(trx);

setup_wdt(WDT_OFF);
strncpy(trx.ident, DEFAULT_STRING, sizeof(DEFAULT_STRING));
fprintf(USB, "\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n"); 
fprintf(USB, "\r\n=[ CPU Restarted, Loading NVM ]=\r\n ");

fprintf(USB, "\r\nTerraterm 4.6.3, (Use Courier 10 Pt Font)\r\n"); 
get_restart_cause();
load_setup_from_nvm();
if (trx.setup_ok != SETUP_PRESENT)
        {      fprintf(USB, "\r\n  Error : No Setup (Use Defaults)");
               init_setup_defaults();
               save_setup_to_nvm();
        }
fprintf(USB,
fprintf(USB, "\r\n...........Identifier : %s ", trx.ident);
fprintf(USB, "\r\n..........Board Ident : %s ", trx.ident);
fprintf(USB, "\r\n....Tracking SP (TSP) : %f (MPa), Ramping SP (RSP) : %03.2f 
(MPa)", trx.tsp, trx.rsp);
fprintf(USB "\r\n..................Rate : %02.1f (KPa/min)", trx.rate);
fprintf(USB, "\r\n..................PID : Kp=%02.2f Ki=%02.2f Kd=%02.2f", trx.Kp, 
trx.Ki, trx.Kd);
fprintf(USB, "\r\n............Direction : %s", trx.fwd);
fprintf(USB,
"\r\n\r\n\r\n");

enable_interrupts(GLOBAL);
disable_interrupts(INT_RDA);
while(1)
	{
        ch = 0 ;	// Needed as it wont return to monitor otherwise. 
        timeout = 15 ;	// load_setup_from_nvm();
        fprintf(USB, "\r\n <ESC> for Setup or Wait for Control Loop\r\n\r\n"); 
        timeout = 6;

        while(timeout--)
               {       ch = timed_getc(50000);
                       restart_wdt(); 
                       if (ch == 27)
                              {
                       fprintf(USB, "\r\n\n <ESC> Pressed : Entering monitor"); 
                              break;
                                      }
                       fprintf(USB, "\r Timeout -[ %Ld ]- ", timeout); 
                       restart_wdt();
                       delay_ms(1000);
               }

        state = 1;
        if (ch == 27)
               while(1){      if (state==1) menu();
                              state = state_machine();
                       }
               else    run_pid();

               fprintf(USB, "\r\n Returning to main program.\r\n ");
        }
}
//***************************************************************************
//      DESCRIPTION:          State Machine for processing user input
//      RETURN:               State Requested by user
//      NOTES:
//***************************************************************************/

int8 state_machine(void)
{       INT8   *arglist[4];
        float Kp, Ki, Kd  ;

float TSP, RSP, RAMP    ;
float vf0, vf1, vf2     ;
float dummy             ;
float rate              ;

char  string[80]        ;
char     fmt[40]        ;
UINT8       n_read=0    ;
UINT8       n_args      ;
int                i    ;

arglist[0]  =     &vf0  ;
arglist[1]  =     &vf1  ;
arglist[2]  =     &vf2  ;

switch(get_state())
{
case 1:
	reset_cpu(); // continue;
case 2:
	fprintf(USB, "\r\n\n         Enter Kp : ");
	get_string(string, sizeof(string));
	strcpy(fmt, "%f");
	sscanf(string, fmt, arglist);
	trx.Kp = vf0;
	// fprintf(USB, " (Kp=%f)", trx.Kp);
	fprintf(USB, "\r\n         Enter Ki : ");
	get_string(string, sizeof(string));
	sscanf(string, fmt, arglist);
	trx.Ki = vf0;
	// fprintf(USB, " (Kp=%f, Ki=%f)", trx.Kp, trx.Ki);
	fprintf(USB, "\r\n         Enter Kd : ");
	get_string(string, sizeof(string));
	sscanf(string, fmt, arglist);
	trx.Kd = vf0;
	fprintf(USB, " (Kp=%f, Ki=%f, Kd=%f)", trx.Kp, trx.Ki, trx.Kd); 
	fprintf(USB, "\r\n\nEnter Rate (KPa/min) : ");
	get_string(string, sizeof(string));
	sscanf(string, "%f", arglist);
	trx.rate = vf0;
	save_setup_to_nvm();
	return(1);
case 3:
	fprintf(USB, "\r\n\nEnter Target Setpoint (TSP) in MPa : "); 
	get_string(string, sizeof(string));
	sscanf(string, "%f", arglist);
	trx.tsp = vf0;
fprintf(USB, "\r\n                   TSP Changed : %f", trx.tsp);
	save_setup_to_nvm();
	return(1);
case 4:
	fprintf(USB, "\r\n\n Note: this sets the calibration using the "); 
	fprintf(USB, "\r\n current ADC value, pressure above this will"); 
	fprintf(USB, "\r\n trigger an alarm condition, which will also"); 
	fprintf(USB, "\r\n occur if the ADC value goes out of bounds"); 
	fprintf(USB, "\r\n Current ADC value : %Lu", read_adc_value(1)); 
	fprintf(USB, "\r\n\n Enter calibration MPa : ");
	getch();
	return(1);
case 5:
	if (strstr(trx.fwd, "Fwd")) strcpy(trx.fwd, "Rev");
	else strcpy(trx.fwd, "Fwd");
	save_setup_to_nvm();
	return(1);
case 6:
	run_pid();
	return(1);        // ** ! Is this correct - Remove ??
case 7:
	init_setup_defaults();
            save_setup_to_nvm();
            return(1);
      case 8:
            save_setup_to_nvm();
            fprintf(USB,"\r\n    Saving : ");
            return(1);
      case 9:
            exercise_dac();
            // init_ad7705(0);
            exercise_adc(0);  // Zero means dont initialise
            exercise_dac();
            fprintf(USB, "\r\n Motor speed: %f", get_motor_rpm(1));
            return(1); 
      default:
            return(0);
      }
}
//***************************************************************************
//    DESCRIPTION:      Converts string pointed to by s to a float
//    RETURN:           None
//    NOTES:            Function: read_eeprom_string
//***************************************************************************/

void read_eeprom_string(char *array, int8 address, int8 max_size)
{
int8  i     =     0;
*array      =     0;

while(i<max_size) 
	{     	*array=read_eeprom(address+i);
		if (*array==0) 
			{ 	i=max_size; } 
		else  	{     	array++;
				*array=0;
			}
		i++;
	}
}
//***************************************************************************
//    DESCRIPTION:      Converts string pointed to by s to a float
//    RETURN:           None
//    NOTES:            Function: write_eeprom_string for saving setup data.
//***************************************************************************

void write_eeprom_string(char *array, int8 address, int8 max_size)
{
int8  i=0;
while(i < max_size) 
	{   	write_eeprom(address+i, *array);
		if (*array==0) 
			{ 	i=max_size; 
			}
		array++; i++;
	}
}
//***************************************************************************
//    DESCRIPTION:      Converts string pointed to by s to a float
//    RETURN:           None
//    NOTES:            Code for PID Loop needs to be added here.
//                      Uses Kp, Ki and Kd to determine loop output.
//***************************************************************************/ 
#define     R_SIZE 5

void 	run_pid(void)
{     	float integ[R_SIZE];
	float P, I, D; 
	float mvstart; 
	float mvnew; 
	float mvlast; 
	float sp, pb, vm; 
	float volts;
	UINT16 ndata, ldata, dac, adc; 
	UINT16 count, lc = 0          ;
	UINT16 reset = R_SIZE;
	UINT16 retries = 0            ;
	INT8  *args[4]                ;
	char command[20], ch          ;
	char  fmt[4]                  ;

	args[0]     =     &sp         ;
	args[1]     =     &pb         ;

	// arglist[1] =   &vf1        ;
	// arglist[2] =   &vf2        ;

	fprintf(USB, "\r\n\nPID Test Program Vo=(Kp*P)+(Ki*I)+(Kd*D)");
	fprintf(USB "\r\nUses Loop Gain only within proportional band"); 
//    	enable_interrupts(INT_RDA);
	enable_interrupts(GLOBAL);
	//    enable_interrupts(INT_TIMER2);
	disable_interrupts(INT_RDA);
	setup_wdt(WDT_ON);
	init_ad7705(1);
	fprintf(USB, "\r\nCH0 Zero : %Lu", read_zero_scale(0)); 
	fprintf(USB, "\r\nCH1 Zero : %Lu", read_zero_scale(1)); 
	fprintf(USB, "\r\n Starting PID Control Loop with (Kp=%f,Ki=%f,Kd=%f)
..<ESC> to Exit.\r\n", trx.Kp, trx.Ki, trx.Kd);
	//    enable_interrupts(INT_RDA);
	//    enable_pulse_width_counter();
	while(1)
            {
            restart_wdt();
            output_toggle(LED_STATUS);
            //    - this runs for interrupt now.
            adc=get_valid_adc_data(0);
            if (count % reset == 0)
                  {     count = 0;
                        mvstart = mvnew;
                  } 
            mvlast = mvnew;
            mvnew = get_mpa(adc);
            if (count == 0)
                  { 
                  D=(mvstart-mvnew)/reset;
                  }
            integ[count] =    trx.tsp-mvnew;
      I = (integ[0]+integ[1]+integ[2]+integ[3]+integ[4])/(R_SIZE*cal.MAX_MPA); 
            P = get_dac_volts(mvnew, trx.tsp, trx.pb);
            if (P != 5 && P!=-5)
                  {
                  volts = (trx.Kp*P)+(trx.Ki*I)+(trx.Kd*D);
                  if (volts > 5) volts = +5;
                  if (volts < -5) volts = -5;
                  }
            else volts = trx.Kp*P;  // Is outside proportional band

            if (volts > 5) volts = 5;
            dac   = get_dac_bits(volts);

            write_dac(dac&0xfff); 
            write_dac(dac&0xfff);

            if (lc % 20 == 0)
		{
fprintf(USB, "\r\nCount:%04Lu, SP:%3.2f,MV:%3.2f,ADC:%05Lu (0x%04LX),", lc, trx.tsp, mvnew, adc, adc);
fprintf(USB, "DAC/PID:%2.2f(V),ERR:%f,(P:%f,I:%f,D:%f),", volts, trx.tsp-mvnew, P,I,D);
fprintf(USB, "RPM:%f", get_motor_rpm(1));
		}
            else  putchar('.');

            count++;    lc++;
            ch    = timed_getc(5000);
            if (ch == 27)
		{     	disable_interrupts(INT_RDA);
                        disable_pulse_width_counter();
                        return;     //    Return a null string
		}
            else if (ch == '?')
		{     	setup_wdt(WDT_ON);
                        disable_interrupts(INT_RDA);
                        enable_interrupts(GLOBAL);
                        fprintf(USB, "\r\nEnter a setpoint: "); 
                        get_string(command, 10); 
                        strcpy(fmt,"%f");
                        sscanf(command, fmt, args);
                        fprintf(USB, "\r\nSetpoint: %f - press a key", sp); 
                        getch();
		}
	}
}
//***************************************************************************
//    DESCRIPTION:      Converts string pointed to by s to a float
//    RETURN:           None
//    NOTES:            Show menu to operator via RS232
//***************************************************************************/

void menu(void)
{
fprintf(USB, "\r\n\r\n ===============[ Triaxial PID Control Operator Console 
]===============\r\n");
fprintf(USB, "\r\nCurrent Parms -> MV : %03.2f (MPa), TSP : %03.2f (MPa), Rate : 
%2.2f (KPa/Min)", trx.mv, trx.tsp, trx.rate);
fprintf(USB, "\r\nConfig PID -> Kp : %3.2f,        Ki : %3.2f,        Kd
: %3.2f \r\n ", trx.Kp, trx.Ki, trx.Kd);
fprintf(USB, "\r\nMode : %s    PB : %f (MPa) \r\n ", trx.fwd, trx.pb );
fprintf(USB, "\r\n\t1. Reset CPU");
fprintf(USB, "\r\n\t2. Enter PID/Rate Values");
fprintf(USB, "\r\n\t3. Enter SP (MPa)");
fprintf(USB, "\r\n\t4. Enter Calibration (MPa)");
fprintf(USB, "\r\n\t5. Toggle direction");
fprintf(USB, "\r\n\t6. Run PID Control Loop");
fprintf(USB, "\r\n\t7. Load Defaults (& Save)");;
fprintf(USB, "\r\n\t8. Save Setup to NVM");
fprintf(USB, "\r\n\t9. DAC, ADC & Encoder Tests");
fprintf(USB, "\r\n\r\n Enter command : ");
}
//***************************************************************************
//    DESCRIPTION:      Display values for PID etc.
//    RETURN:           None
//    NOTES:
//***************************************************************************/

void show_values(void)
{
fprintf(USB, "\r\nLoaded from NVM : (Checks)");
fprintf(USB, "\r\n.............Kp : %f ", trx.Kp);
fprintf(USB, "\r\n.............Ki : %f ", trx.Ki);
fprintf(USB, "\r\n.............Kd : %f ", trx.Kd);
fprintf(USB, "\r\n...........Rate : %f ", trx.rate);
fprintf(USB, "\r\n............TSP : %f ", trx.tsp);
fprintf(USB, "\r\n............RSP : %f ", trx.rsp);
fprintf(USB, "\r\n              PB : %f ", trx.pb);
fprintf(USB, "\r\n           Setup : %02x \r\n", trx.setup_ok);
}
//***************************************************************************
//     DESCRIPTION:        Converts string pointed to by s to a float
//     RETURN:             None
//     NOTES:
//***************************************************************************/

void init_setup_defaults(void)
{
init_ad7705(1);
fprintf(USB, "\r\n\n================( Loading/Saving default Setup Values 
)================\r\n ");
//     Load calibration defaults ...
cal.LV_BITS =        12000;
cal.HV_BITS =        60000;
cal.MAX_MPA =        300 ;

strncpy(trx.ident,  DEFAULT_STRING, sizeof(DEFAULT_STRING));
trx.Kp     = 5.0          ;
trx.Ki     = 0.1                         ;
trx.Kd     = 0.1                         ;
trx.rsp    = 15.5                        ;// Ramping setpoint in MPa (max 300)
trx.tsp    = 220.0                       ;// Target setpoint in MPa
trx.rate   = 20.0                        ;// Ramp rate in KPa/min
trx.pb   = 20.0                          ;// Proportional band in MPa
trx.mverr = -1                           ;// Null value MV is 0 initially
strcpy(trx.fwd,  "Fwd");                 ;// Forward acting PID loop
trx.mv = get_mpa(get_valid_adc_data(0));// Null value is stored 
trx.setup_ok = SETUP_PRESENT             ;// Setup marked as OK.

fprintf(USB, "\r\n              Status : Writing NVM Setup");
save_setup_to_nvm();
show_values();
}
//***************************************************************************
//     DESCRIPTION:        Converts string pointed to by s to a float
//     RETURN:             None
//     NOTES:
//***************************************************************************/

UINT8  get_state(void)
{      BYTE ch;
ch  =  getchar();
putchar(ch);
if  (ch==  '1') return(1);
if  (ch==  '2') return(2);
if  (ch==  '3') return(3);
if  (ch==  '4') return(4);
if  (ch==  '5') return(5);
if  (ch==  '6') return(6);
if  (ch==  '7') return(7);
if  (ch==  '8') return(8);
if  (ch==  '9') return(9);
return(0);
}
//***************************************************************************
//     DESCRIPTION:        Converts string pointed to by s to a float
//     RETURN:             None
//     NOTES:              Here is where the setup structure is read from NVM
//                         This defines PID,  rate, SP, direction etc
//***************************************************************************/

void load_setup_from_nvm(void)
{
fprintf(USB, "\r\n       Reading Setup : (Size : %Ld Bytes)", sizeof(trx));
fprintf(USB, "\r\n         Data EEPROM : (Size : %Ld Bytes)", 
getenv("DATA_EEPROM"));
read_eeprom_string(&buffer[0], 0, sizeof(trx));
memcpy(&trx, &buffer[0], sizeof(trx));

if (trx.setup_ok == SETUP_PRESENT)
       fprintf(USB, "\r\n         Setup Read : Ok\r\n");
else   fprintf(USB, "\r\n          First Run : No Setup");
}
//***************************************************************************
//     DESCRIPTION:        Converts string pointed to by s to a float
//     RETURN:             None
//     NOTES:              Setup structure writes setup data to the NVM
//***************************************************************************/

void save_setup_to_nvm(void)
{
memcpy(&buffer[0], &trx, sizeof(trx));
write_eeprom_string(&buffer[0], 0, sizeof(trx));
}
//***************************************************************************
//     DESCRIPTION:        Converts string pointed to by s to a float
//     RETURN:             None
//     NOTES:              Clearing Setup Values from Memory
//***************************************************************************/

void clear_structure(void)
{ 
fprintf(USB,  "\r\n Setup values cleared from memory : ");
trx.Kp     = 0;  trx.Ki  = 0;  trx.Kd   = 0;
trx.rate   = 0;  trx.rsp = 0;  trx.tsp  = 0;
trx.tsp    = 0;  trx.mv  = 0;  trx.pb   = 0;
trx.mverr  = 0;  trx.setup_ok = 0;
strcpy(trx.fwd, "\0");
}
//***************************************************************************
//     DESCRIPTION:        Converts string pointed to by s to a float
//     RETURN:             None
//     NOTES:              Clearing NVM (EEPROM)
//***************************************************************************/

void erase_nvm(int16 size)
{             int16 i=0;
              fprintf(USB, "\r\n Erasing Seup EEPROM %Ld Bytes 0\r\n", size); 
              for (i=0; i < sizeof(trx); )
                     {            write_eeprom(i++, 0xff);
                     }            check_erased();
}
//***************************************************************************
//     DESCRIPTION:        Converts string pointed to by s to a float
//     RETURN:             None
//     NOTES:              Now check that the NVM was erased corretly.
//***************************************************************************/

void check_erased(void)
{    int16 i=0, val;
     fprintf(USB, "\r\nCHECK_ERASED()\r\n");
     for (i=0; i > sizeof(trx); )
     	{   val = read_eeprom(i);
            if (val != 0xff) fprintf(USB, "\r\n Error EEPROM not Erased");
                 i++;
     	}
}
//***************************************************************************
//     DESCRIPTION:        Converts string pointed to by s to a float
//     RETURN:             None
//     NOTES:              Process Restart Cause
//***************************************************************************/

void get_restart_cause(void)
{             fprintf(USB, "\r\n    RESTART_CAUSE() : ");
switch(restart_cause())
	{
	case NORMAL_POWER_UP:      fprintf(USB, "Normal Startup !"); return;
	case WDT_FROM_SLEEP:       fprintf(USB, "WDT from Sleep");    return;
	case WDT_TIMEOUT:          fprintf(USB, "WDT Timeout");       return;
	case BROWNOUT_RESTART:     fprintf(USB, "Brownout Reset");    return;
	case RESET_INSTRUCTION:    fprintf(USB, "Reset Instruction"); return;
	case MCLR_FROM_SLEEP:      fprintf(USB, "MCLR from sleep");   return;
	case MCLR_FROM_RUN:        fprintf(USB, "MCLR from run");     return;
	default:                   fprintf(USB, "New flash ?");       return;
       }
}
//***************************************************************************
//     DESCRIPTION:        Converts string pointed to by s to a float
//     RETURN:             None
//***************************************************************************/

void 	get_string(char* s, int max) 
{ 
	int    	len;
	char 	c;

	--max;
	len=0;
	do     	{	c=getc();
			if(c==8)     
				{ // Backspace
				if(len>0)     
					{	len--;
						putc(c); 
						putc(' ');
						putc(c);
					}
		} 		else  	if ((c>=' ') && (c<='~'))
					if(len<max) 
						{
                                                s[len++]=c;
                                                putc(c);
                                                }
		} while(c != 13);
	s[len]=0;
}
//***************************************************************************
//     DESCRIPTION:        Converts string pointed to by s to a float
//     RETURN:             None
//     NOTES:              Timed version of getc to stop holding up program
//***************************************************************************

char 	timed_getc(long       value)
{       long timeout;       // timeout_error=FALSE;
	timeout = 0;

	if (value < 0) value = 5000;
	while ( !kbhit() && ( ++timeout < value ) ) 
	delay_us(10);

if (kbhit()) 	return(getc());
else           	return(0);
}
//***************************************************************************
//     DESCRIPTION:        Converts string pointed to by s to a float
//     RETURN:             None
//***************************************************************************

UINT8 sscanf(
  char *buf,    /* pointer to the buffer that we are scanning     */
  char *fmt,    /* pointer to the format string                   */
  char *pArgs) 	/* pointer to array of arguments                  */ 
	{
	int8 count = 0;
	char *p;
	int1 size_long; 
	char *endptr;

	while (1)
		{  	// Look to see if we are out of arguments 
		if ( !pArgs )
			return( count );
			// Gobble up the fmt string 
		while (*buf == *fmt)
			{	if ((*buf == 0) || (*fmt == 0))
					return (count);
				buf++; 
				fmt++; 
			} // Check for the % 
		if (*fmt != '%')
			break; 
		
		/* fmt should be '%' go to the next character */
		
		fmt++; /* get the size modifier */
		switch (*fmt) 
			{	case 'l':
				case 'L':
					fmt++;
				size_long = TRUE;
				break; 
				default:
					size_long = FALSE;
					break;
			}   
		/* fmt should point to our first 
		conversion letter at this point */

switch (*fmt) 
{
 case 'f': 
 case 'F':					/* Get a pointer to this argument */
  p = (float *)(*pArgs);			/* convert to a number */
  *(float *)p = (float)strtod(buf, &endptr);	/* Make sure that we succeeded */ 
  if ( buf == endptr )
	return ( count );
  buf = endptr;					/* count this one */ 
  count++;
  break;

  while (1)
	{	if ((isspace(*buf)) || (!*buf))
			{	*p = 0;
				break;
			} 
		else	{	*p = *buf;
				p++;
				buf++;
			}
	}        
	/* count this one */
  count++;
  break;   					/* unhandled format specifier */
 default:
	return (count);
}                         
/* Technically this is incorrect but since the size of all pointers
are the same, who cares ;) point to the next argument  */

     	pArgs += sizeof(char *);                                               

/* Move to the next format char */

	fmt++;
   	}    return (count);
}
//***************************************************************************
//      DESCRIPTION:   Initialises ADC
//      RETURN:        None
//      ALGORITHM:     None
//*************************************************************************** 
void calibrate_adc(void) {  init_ad7705(1);  }
//***************************************************************************
//      DESCRIPTION:   Initialises ADC
//      RETURN:        None
//      ALGORITHM:     None
//***************************************************************************

void init_ad7705(int1 type) 
{
setup_wdt(WDT_OFF);

output_low(ADC_RESET); 
output_high(ADC_CLK); 
output_high(AD7705_CS); 
output_high(ADC_RESET);

fprintf(USB, "\r\n");
delay_ms(1000);        
fprintf(USB, "\r...Initialising AD7705 ADC:  (3 Seconds)");
delay_ms(1000);        
fprintf(USB, "\r...Initialising AD7705 ADC:  (2 Seconds)");
delay_ms(1000);        
fprintf(USB, "\r...Initialising AD7705 ADC:  (1 Second)  ");
if (type)
        {
        read_adc_value(0); 
        setup_ad7705(ADC_NORMAL,         ADC_GAIN_1,   0x4, ADC_50,  0x2,  0x0);
        delay_ms(100); 
        setup_ad7705(ADC_ZERO_SCALE,     ADC_GAIN_1,   0x4, ADC_50,  0x2,  0x1);
        delay_ms(100);
        setup_ad7705(ADC_NORMAL,         ADC_GAIN_1,   0x4, ADC_50,  0x0,  0x0);
        delay_ms(100); 
        fprintf(USB, "\r\n\n "); 
        read_adc_value(1); 
        setup_ad7705(ADC_NORMAL,         ADC_GAIN_1,   0x4, ADC_50,  0x2,  0x0);
        delay_ms(100);
        setup_ad7705(ADC_ZERO_SCALE,     ADC_GAIN_1,   0x4, ADC_50,  0x2,  0x1);
        delay_ms(100);
        setup_ad7705(ADC_NORMAL,         ADC_GAIN_1,   0x4, ADC_50,  0x0,  0x0);
        delay_ms(100);
        //     read_adc_value(1);
        }      setup_wdt(WDT_ON);
}
//*************************************************************************** 
//      DESCRIPTION: Converts string pointed to by s to a float
//      RETURN:        None
//      ALGORITHM:     None
//***************************************************************************

void setup_ad7705(BYTE calmode, BYTE gainsetting, BYTE operation, BYTE rate, 
BYTE fsync, BYTE buffered)
{              write_adc_byte(0x20);          // Communications register
               write_adc_byte(rate);
               write_adc_byte(0x10);          // Access setup register.
               write_adc_byte(calmode|gainsetting|operation|fsync|buffered);
}
//***************************************************************************
//    DESCRIPTION: Converts string pointed to by s to a float
//    RETURN:     Result of the conversion
//    ALGORITHM:  None
//*************************************************************************** 
// And now the functions for the ADC AD7705

void 	write_adc_byte(BYTE data) 
{       BYTE  i;

        output_low(AD7705_CS);
        for (i=1; i<=8;++i)
        	{     output_low(ADC_CLK);
                      output_bit(ADC_DI, shift_left(&data,1,0)); 
                      output_high(ADC_CLK);
                }     output_high(AD7705_CS);
}
//*************************************************************************** 
//    DESCRIPTION: Converts string pointed to by s to a float
//    RETURN:      None
//***************************************************************************/

void 	exercise_adc(int1 init)
{
#define NCONVS 1
UINT16 	data;
UINT16 	i=20;      
//    , lc=0,
UINT16 	nc=0; 
UINT16 	retries=0;
fprintf(USB,"\r\n Exercising ADC AD7705 \r\n");
setup_wdt(WDT_ON);

if (init)   
	init_ad7705(1);
while(i--)
      {     output_toggle(LED_STATUS);
            restart_wdt();
            delay_ms(200);
            data=get_valid_adc_data(0);
            //if (lc++ % NCONVS == 0 && lc != 1 && data !=0xffff)
            fprintf(USB, "\r\n Convs %03Lu, ADC: %Lu", nc+=NCONVS, data);
            //          retries = 0, lc   = 0;
            //    }
            if (data >= 0xfff0)
                  {     read_adc_value(1);
                        retries=0;
                        //if (retries > 4)
            //    {     fprintf(USB, "\rRetries : %Ld (Reset ADC)", retries);
            //          init_ad7705(1);
            //          retries=0;
            //    }
                        }
            }
}
//***************************************************************************
//    DESCRIPTION:      Converts string pointed to by s to a float
//    RETURN:           None
//***************************************************************************/

long int read_adc_word() 
{        BYTE i;
         Long data;

         output_low(AD7705_CS);
         for (i=1;i<=16;++i)
            	{ 	output_low(ADC_CLK);
            		output_high(ADC_CLK);
            		shift_left(&data,2, input(ADC_D0));
            	}
}
