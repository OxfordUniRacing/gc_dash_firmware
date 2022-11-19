#include <atmel_start.h>
#include <stdio.h>
#include <string.h>
#include "DisplayNumbersAndText.h"
#include "SPI_io_descriptor.h"
#include "USER_DEF.h"


// @@ NOTE ALSO NEED TO INCLUDE A MESSAGE FROM THE VCU TO SAY TO POWER CYCLE THE LV NOT IMPLEM
//NOTE: SPI baud rate changed to 1Mbaud in config file but not atmel start in the (sercom config file)

// Variables Global

// add timeout - make relay steet keeplive
// For the screen
int speed = 0;
int battery_charge = 100;
int battery_v = 0;
int battery_temp = 0;

bool AMS_state = 0; // start the AMS light off, no issues


// Dealing with the timer
static int display_ms_counter = 0;
static int recive_can_bms_counter = 0;
static int last_state = 0;

#define CAN_RX_BUFFER_BYTES 8
#define CAN_RX_BUFFER_LENGTH 10

#define DISCHARGE_STATE_BIT 8
#define CHARGE_SAFETY_BIT 6

bool charge_safety;
bool discharge_state;


//Define Buffer - Needs to be volatile, but have to write volatile memcpy function
uint8_t CAN_speed_buffer[CAN_RX_BUFFER_LENGTH][CAN_RX_BUFFER_BYTES] = {{0}}; //Can store 10 messages, each message has 8 bytes (Assuming they all have the same ID)
uint8_t CAN_battery_charge_and_temp_buffer[CAN_RX_BUFFER_LENGTH][CAN_RX_BUFFER_BYTES] = {{0}};

uint8_t CANams[CAN_RX_BUFFER_LENGTH][CAN_RX_BUFFER_BYTES] = {{0}};

volatile uint8_t CAN_speed_buffer_head = 0;		//The position for the next message to go into
volatile uint8_t CAN_battery_charge_and_temp_buffer_head = 0;
volatile uint8_t CAN_AMS_buffer_head = 0;

// Timer
static struct timer_task TIMER_0_task1; // timer to display screen
static struct timer_task TIMER_AMS_task; // task to unlatch after the first three seconds
static struct timer_task TIMER_KEEP_ALIVE_AMS; // keep alive battery


// for tt1
static void TIMER_0_task1_cb(const struct timer_task *const timer_task)
{
	display_ms_counter++; // toggle the counter for the display, this is instead of delays
}

static void TIMER_ams_task_cb(const struct timer_task *const timer_task)
{
	// Use this to reset the latch of the RED ams light
	gpio_set_pin_level(LEDYellow,0); // @@ replace with the red light when available.
}

static void TIMER_keep_alive_ams_cb(const struct timer_task *const timer_task)
{
	//check if the previous one has incremented
	if(recive_can_bms_counter <= last_state+50)
	{
		gpio_set_pin_level(LEDYellow,1); // toggle one
	}
	else
	{
		last_state = recive_can_bms_counter;
	}
	
}

static void CAN_0_tx_callback(struct can_async_descriptor *const descr) // just to not make it angry
{
	(void)descr;
}

// can handler
static void CAN_0_rx_callback(struct can_async_descriptor *const descr) // This is an interrupt, we will parse the messages
{

	
	struct can_message msg;
	uint8_t            data[8];
	msg.data = data;
	can_async_read(descr, &msg);
	
	// Switch statement to check and parse the information
	
	switch (msg.id)
	{
		// 0xnumbers means numbers in base 16
		case 0x009: ;// AMS LIGHT READ RELAY STATE
		// It is 2 bytes, bit 0 is the discharge relay and bit 2 is the charge safety, if both 0, turn the light on.
		// Perform logic directly
		
		// used for testing the logic
		////memcpy(CANams[CAN_AMS_buffer_head],msg.data,2);
		////CAN_AMS_buffer_head++;
		////
		////if(CAN_AMS_buffer_head >= CAN_RX_BUFFER_LENGTH)
		////{
			////CAN_AMS_buffer_head = 0;
		////}
		
		// done testing
		// increm counter
		recive_can_bms_counter++;
		
		charge_safety = ((msg.data[1]>>CHARGE_SAFETY_BIT)&1);
		discharge_state = ((msg.data[1]>>DISCHARGE_STATE_BIT)&1);
		
		if(charge_safety&&discharge_state == 0) // There is an issue, latch the LED
		{
			gpio_set_pin_level(LEDYellow,1);
		}
		
		break;
		case 0x00A: // Temperature from battery in byte 1, in byte 3 is the charge
		memcpy(CAN_battery_charge_and_temp_buffer[CAN_battery_charge_and_temp_buffer_head],msg.data,4);
		CAN_battery_charge_and_temp_buffer_head++;
		
		if(CAN_battery_charge_and_temp_buffer_head >= CAN_RX_BUFFER_LENGTH)
		{
			CAN_battery_charge_and_temp_buffer_head = 0;
		}
		
		break;
		
		default:
		break;
		
	}
	
	return;
}

// code to turn can screen into display log
void create_log()
{
	startFrame();
	// This was for the can display
	for(uint8_t j = 0; j < CAN_RX_BUFFER_LENGTH; j++)
	//uint8_t j = 0;
	{
	char temp_str[256];	//Placing 64 bytes represented as hex => 64*3 minimum chars needed
	
	char* temp_ptr = temp_str;
	for(uint8_t i = 0; i < 16; i++)
	{
	temp_ptr += snprintf(temp_ptr, 4, "%02x ",CANams[j][i]);
	}
	display_text(50, 25*j, 16, temp_str);
	}
	char temp_str[15];

	display_numberRight((int)(WIDTH-(0.03*WIDTH)),(int)((HEIGHT*0.3333)+(0.03*HEIGHT)),1,battery_v);
	endFrame();
}

void display_handler()
{
	static int framenum = 0;
	// Everything must happen within the Start and End frame placeholders
	startFrame();
	
	// Screen Display
	
	// SPEEDOMETER
	display_gauge((framenum%gauge_range)); // sPEEDOMETER GAUGE
	display_text((int)(gauge_x0-(gauge_radius*0.7)),10,22,"SPEEDOMETER"); // TITLE
	display_text((int)(gauge_x0-(gauge_radius*0.1)),(int)(HEIGHT-(0.05*HEIGHT)),20,"KM/H"); // UNIT
	
	// SEPARATOR LINE (SECTION, SPEEDOMETER)
	display_line(VERT_X,0, VERT_X, HEIGHT, 3);
	
	// SERPARATOR LINES, RIGHT HAND SIDE SECTIONS
	display_line(VERT_X,(int)(HEIGHT*(0.3333)),WIDTH,(int)(HEIGHT*(0.3333)),3);
	display_line(VERT_X,(int)(HEIGHT*(0.6667)),WIDTH,(int)(HEIGHT*(0.6667)),3);
	
	// BATTERY CHARGE - read it from the buffer - third byte
	display_progress((CAN_battery_charge_and_temp_buffer[CAN_battery_charge_and_temp_buffer_head-1][3])/2);
	display_text((int)(VERT_X+(0.05*WIDTH)),10,22,"BATTERY CHARGE"); // TITLE
	
	// reset the buffer (later as i need to do the second part of the message)
	
	// BATTERY TEMP
	display_text((int)(VERT_X+(0.05*WIDTH)),(int)((HEIGHT*0.3333)+(0.03*HEIGHT)),22,"BATTERY"); // TITLE
	display_text((int)(VERT_X+(0.05*WIDTH)),(int)((HEIGHT*0.3333)+(0.13*HEIGHT)),22,"TEMP"); // TITLE
	display_text((int)(VERT_X+(0.05*WIDTH)),(int)((HEIGHT*0.3333)+(0.23*HEIGHT)),22,"(degC)"); // TITLE
	// TO SET A FONT HIGHER THAN 31:
	FT8_cmd_romfont(1,33);
	//display_numberRight((int)(WIDTH-(0.03*WIDTH)),(int)((HEIGHT*0.3333)+(0.03*HEIGHT)),1,30); // The last argument is the value
	// the actual
	//display_numberRight((int)(WIDTH-(0.03*WIDTH)),(int)((HEIGHT*0.3333)+(0.03*HEIGHT)),1,CAN_battery_charge_and_temp_buffer[CAN_battery_charge_and_temp_buffer_head-1][1]); // The last argument is the value
	// jack testing
	//display_numberRight((int)(WIDTH-(0.03*WIDTH)),(int)((HEIGHT*0.3333)+(0.03*HEIGHT)),1,(int)discharge_state); // The last argument is the value
	
	// reset the buffer
	CAN_battery_charge_and_temp_buffer_head = 0;
	
	// MOTOR TEMP @@ NOT YET IMPLEMENTED - WILL BE SENT FROM VCU
	display_text((int)(VERT_X+(0.05*WIDTH)),(int)((HEIGHT*0.6667)+(0.03*HEIGHT)),22,"MOTOR"); // TITLE
	display_text((int)(VERT_X+(0.05*WIDTH)),(int)((HEIGHT*0.6667)+(0.13*HEIGHT)),22,"TEMP"); // TITLE
	display_text((int)(VERT_X+(0.05*WIDTH)),(int)((HEIGHT*0.6667)+(0.23*HEIGHT)),22,"(degC)"); // TITLE
	// TO SET A FONT HIGHER THAN 31:
	FT8_cmd_romfont(1,33);
	// the actual
	display_numberRight((int)(WIDTH-(0.03*WIDTH)),(int)((HEIGHT*0.6667)+(0.03*HEIGHT)),1,47); // The last argument is the value
	// jack testing
	//display_numberRight((int)(WIDTH-(0.03*WIDTH)),(int)((HEIGHT*0.6667)+(0.03*HEIGHT)),1,(int)charge_safety);
	
	framenum = framenum+1;
	
	endFrame();
}


int main(void)
{
	/* Initializers */
	atmel_start_init();
	create_spi_descriptor();
	FT8_init();
	
	struct can_message msg;
	struct can_filter  filter;
	// can receive
	can_async_register_callback(&CAN_0, CAN_ASYNC_TX_CB, (FUNC_PTR)CAN_0_tx_callback); // do not anger the god
	can_async_register_callback(&CAN_0, CAN_ASYNC_RX_CB, (FUNC_PTR)CAN_0_rx_callback);
	
	filter.id   = 0; // receive all messaged
	filter.mask = 0;
	can_async_set_filter(&CAN_0, 0, CAN_FMT_STDID, &filter);
	
	// timer set up
	
	TIMER_0_task1.interval = 1; // 1 ms interrupt
	TIMER_0_task1.cb       = TIMER_0_task1_cb;
	TIMER_0_task1.mode     = TIMER_TASK_REPEAT;
	timer_add_task(&TIMER_0, &TIMER_0_task1);
	
	TIMER_AMS_task.interval = 3000; // Interrupt after three seconds
	TIMER_AMS_task.cb       = TIMER_ams_task_cb;
	TIMER_AMS_task.mode     = TIMER_TASK_ONE_SHOT;
	timer_add_task(&TIMER_0, &TIMER_AMS_task);
	
	
	TIMER_KEEP_ALIVE_AMS.interval = 100; // Interrupt after three seconds
	TIMER_KEEP_ALIVE_AMS.cb       = TIMER_keep_alive_ams_cb;
	TIMER_KEEP_ALIVE_AMS.mode     = TIMER_TASK_REPEAT;
	timer_add_task(&TIMER_0, &TIMER_KEEP_ALIVE_AMS);
	
	timer_start(&TIMER_0);
	
	/* Functionality Loop */
	
	
	while (1) {
		
		if (display_ms_counter > 100) // Display every 100 ms
		{
			display_handler();
			//create_log();
			display_ms_counter = 0;
		}
		
		
	}
}
