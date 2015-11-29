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

int la_init_controls(int** fdControls, int* fdControlCount)
{
	int ret;

	ret = wiringPiSetup();
	if(ret) return ret;

	if ((fdsArduino[0] = serialOpen ("/dev/ttyAMA0", 9600)) < 0)
    {
    	fprintf (stderr, "E: Unable to open serial device: %s\n", strerror (errno)) ;
    	return -1;
    }

    serialFlush(fdsArduino[0]);

    fArduino = fdopen(fdsArduino[0], "r");

    buf_len = 128;
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

void la_leds_off()
{
	fprintf(stdout, "OFF\n");
	//serialPuts(fdsArduino[0], "PI: HELLO\n");
	//serialFlush(fdsArduino[0]);
	serialPutchar (fdsArduino[0], 'J');
	serialFlush(fdsArduino[0]);
}

void la_leds_on()
{
	//serialPutchar (fdsArduino[0], 'N');
	//serialFlush(fdsArduino[0]);
}

int la_control_input_one(int fd)
{
	Control c;
	int len;
	const char* cmd;


	len = getline(&buf, &buf_len, fArduino);
	if(len == 0)
	{
		fprintf(stderr, "E: nothing read from arduino\n");
		return -1;
	}
	else if(strstr(buf, "IR: ") != buf)
	{
		fprintf(stdout, "arduino: %s\n", buf);
		return 0;
	}
	else if(buf[len - 1] != '\n')
	{
		fprintf(stdout, "arduino no endl: %s\n", buf);
		return 0;
	}
	
	buf[len - 2] = '\0';
	cmd = buf + 4;
	if(!strcmp("POWER", cmd))
	{
		c = LA_PLAYPAUSE;
	}
	else if(!strcmp("UP", cmd))
	{
		c = LA_UP;
	}
	else if(!strcmp("SETUP", cmd))
	{
		c = LA_MENU;
	}
	else if(!strcmp("LEFT", cmd))
	{
		c = LA_LEFT;
	}
	else if(!strcmp("ENTER", cmd))
	{
		c = LA_OK;
	}
	else if(!strcmp("RIGHT", cmd))
	{
		c = LA_RIGHT;
	}
	else if(!strcmp("DOWN", cmd))
	{
		c = LA_DOWN;
	}
	//else if(!strcmp("VOL+", cmd))
	//{
	//}
	//else if(!strcmp("VOL-", cmd))
	//{
	//}
	else
	{
		fprintf(stderr, "unsupported command: '%s'\n", cmd);
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