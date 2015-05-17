#include "controles.h"

#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <errno.h>
#include <string.h>

#include <wiringPi.h>

#define	DEBOUNCE_TIME	200
static Callback callbacks[LA_CONTROL_LENGTH] = {0};
static void* callback_params[LA_CONTROL_LENGTH] = {0};

static int PINS_1[4] = {3, 12, 13, 14};
static int PINS_2[4] = {26, 27, 28, 29};

static int debounceTime[2] = {0};

static void handlePins(int i)
{
	if (millis () < debounceTime[i])
	{
		debounceTime[i] = millis() + DEBOUNCE_TIME;
		//printf("bouncing\n");
		return;
	}
	
	la_control_input_one(i+1);
	debounceTime[i] = millis() + DEBOUNCE_TIME;
}

static void handlePins1()
{
	handlePins(0);
}
static void handlePins2()
{
	handlePins(1);
}

int la_init_controls(int** fdControls, int* fdControlCount)
{
	int ret;
	int i;
	
	ret = wiringPiSetup();
	if(ret) return ret;

	for(i=0;i<4;i++)
	{
		pinMode(PINS_1[i], INPUT);
		pullUpDnControl(PINS_1[i], PUD_DOWN);
		pinMode(PINS_2[i], INPUT);
		pullUpDnControl(PINS_2[i], PUD_DOWN);
	}
	
    ret = wiringPiISR(PINS_1[0], INT_EDGE_BOTH, handlePins1);
    if(ret){
    	fprintf(stderr, "E: setup ISR pin %i\n",PINS_1[0]);
    	return ret;
    }
    
    ret = wiringPiISR(PINS_2[0], INT_EDGE_BOTH, handlePins2);
    if(ret){
    	fprintf(stderr, "E: setup ISR pin %i\n", PINS_2[0]);
    	return ret;
    }

	*fdControls = NULL;
	*fdControlCount = 0;
	return 0;
}

void la_on_key(Control ctrl, Callback fn, void* param)
{
	if(ctrl>=0 && ctrl < LA_CONTROL_LENGTH)
	{
		callbacks[ctrl] = fn;
		callback_params[ctrl] = param;
	}
}

void la_wait_input()
{
	fprintf(stderr, "E: UNSUPPORTED la_wait_input\n");
	exit(-1);
}

// PINS 1
// #define CODE_ 0xf
// #define CODE_PLAY 0x7
// #define CODE_REW 0x1
// #define CODE_CHUP 0x3

// PINS 2
#define CODE_POWER 0xf
#define CODE_PLAY 0x7
#define CODE_REW 0x1
#define CODE_CHDWN 0x3

int la_control_input_one(int fd)
{
	int pins = 0;
	int i;
	Control c;
	int countDebounce = 0;
	int pinsDebounce = 0;
	
	delay(300);
	for(countDebounce = 0; countDebounce < 1; countDebounce++)
	{
		pins = 0;
		for(i=0 ; i < 4 ; i++)
		{
			pins |= digitalRead(fd == 1 ? PINS_1[i] : PINS_2[i]) << i;
		}
		if(countDebounce == 0)
		{
			pinsDebounce = pins;
		}
		else
		{
			if(pins != pinsDebounce)
			{
				countDebounce = 0;
				pinsDebounce = pins;
				printf("debounce %x\n", pins);
			}
		}
		delay(1);
	}
	
	printf("out\n");

	if(fd == 2)
	{
		if(pins == CODE_POWER)
		{
			c = LA_MENU;
		}
		else if(pins == CODE_PLAY)
		{
			c = LA_PLAYPAUSE;
		}
		else if(pins == CODE_CHDWN)
		{
			c = LA_DOWN;
		}
		else
		{
			return 0;
		}
	}
	else
	{
		printf("D: pb pins 1\n");
		return 0;
	}
	
	printf("D: la_control_input_one %i %x %s\n", fd, pins, DEBUG_CODES[c]);
	if(callbacks[c] != NULL)
	{
		return callbacks[c](c, callback_params[c]);
	}
	return 0;
}

void la_exit()
{
}