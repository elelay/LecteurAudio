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
#include <wiringSerial.h>

static Callback callbacks[LA_CONTROL_LENGTH] = {0};
static void* callback_params[LA_CONTROL_LENGTH] = {0};

static int fdsArduino[1] = {-1};
static FILE* fArduino = NULL;

static char* buf;
static size_t buf_len;

typedef enum {
	CODE__1,
	CODE_POWER,
	CODE_PLAY,
	CODE_DOWN,
	CODE_REW,
	
	CODE__2,
	CODE_STOP,
	CODE_REC,
	CODE_UP,
	CODE_FF,

	CODE_LENGTH
} Code;

static char* DEBUG_CODES[CODE_LENGTH] = {
	"_1",
	"POWER",
	"PLAY",
	"DOWN",
	"REW",
	
	"_2",
	"STOP",
	"REC",
	"UP",
	"FF",
};

static char* DEBUG_CMDES[LA_CONTROL_LENGTH] = {
	"LA_PLAYPAUSE",
	"LA_UP",
	"LA_DOWN",
	"LA_MENU",
	"LA_OK"
};

int la_init_controls(int** fdControls, int* fdControlCount)
{
	int ret;
	
	ret = wiringPiSetup();
	if(ret) return ret;

	if ((fdsArduino[0] = serialOpen ("/dev/ttyACM0", 9600)) < 0)
    {
    	fprintf (stderr, "E: Unable to open serial device: %s\n", strerror (errno)) ;
    	return -1;
    }
    fArduino = fdopen(fdsArduino[0], "r");
    
    buf_len = 8;
    buf = malloc(buf_len);
    if(!buf)
    {
    	fprintf (stderr, "E: Unable to allocate buffer: %s\n", strerror (errno)) ;
    	return -1;
    }
    
	*fdControls = fdsArduino;
	*fdControlCount = 1;
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

int la_control_input_one(int fd)
{
	int val;
	Control c;
	int len;
	

	len = getline(&buf, &buf_len, fArduino);
	if(len == 0)
	{
		fprintf(stderr, "E: nothing read from arduino\n");
		return -1;
	}
	else if(len != 8){
		fprintf(stderr, "E: invalid read (%i) from arduino:%s\n", len, buf);
		return -1;
	}
	
	if(!strstr(buf, "PIN1 "))
	{
		val = buf[5] - '0';
	}
	else if(!strstr(buf, "PIN2 "))
	{
		val = buf[5] - '0' + 5;
	}
	else
	{
		fprintf(stderr, "E: invalid line read from arduino: %s\n", buf);
	}
	if(val < 0 || val > 9)
	{
		fprintf(stderr, "E: invalid line read from arduino: %s\n", buf);
	}
	//printf("D: PIN %s\n",DEBUG_CODES[val]);
	
	switch(val)
	{
	case CODE_POWER:
		c = LA_MENU;
		break;
		
	case CODE_PLAY:
		c = LA_PLAYPAUSE;
		break;
		
	case CODE_UP:
	case CODE_REW:
		c = LA_UP;
		break;
		
	case CODE_DOWN:
	case CODE_FF:
		c = LA_DOWN;
		break;
		
	case CODE_REC:
		c = LA_OK;
		break;

	default:
		return 0;
	}
	
	if(callbacks[c] != NULL)
	{
		return callbacks[c](c, callback_params[c]);
	}
	return 0;
}

void la_exit()
{
}