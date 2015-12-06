#include "controles.h"
#include "ecran.h"

#include <sys/stat.h>
#include <fcntl.h>
#include <locale.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <errno.h>
#include <string.h>

#include <iconv.h> 

#include <wiringPi.h>
#include <wiringSerial.h>

static Callback callbacks[LA_CONTROL_LENGTH] = {0};
static void* callback_params[LA_CONTROL_LENGTH] = {0};

static int fdsArduino[1] = {-1};
static FILE* fArduino = NULL;

static char* buf;
static size_t buf_len;

static iconv_t conv;
static const size_t conv_buf_len = 64;
static char conv_buf[64];
static int saved_x = 0, saved_y = 0;

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
	//serialPutchar (fdsArduino[0], 'J');
	//serialFlush(fdsArduino[0]);
}

void la_leds_on()
{
	//serialPutchar (fdsArduino[0], 'N');
	//serialFlush(fdsArduino[0]);
}

int la_init_ecran()
{
	setlocale (LC_ALL, "");

  	conv = iconv_open("ISO-8859-1", "UTF-8");
  	if(conv == (iconv_t)(-1))
  	{
  		fprintf(stderr, "E: iconv init failed: %s\n", strerror(errno));
  	}
  	la_lcdHome();
  	la_lcdPuts("HELLO WORLD123");
  	//usleep(5000000);
	return 1;
}

void la_lcdHome()
{
	la_lcdPosition(0,0);
}

void la_lcdClear()
{
	fprintf(stdout,"D: sending PL\n");
	serialPuts(fdsArduino[0], "PL\n");
	//serialFlush(fdsArduino[0]);
	usleep(5000);
	//NUGET getchar()
}

void la_lcdPosition(int col, int row)
{
	fprintf(stdout,"D: sending PG%02i%02i\n", col, row);
	serialPrintf(fdsArduino[0], "PG%02i%02i\n", col, row);
	//serialFlush(fdsArduino[0]);	
	saved_x = col;
	saved_y = row;
	usleep(5000);
	//NUGET getchar()
}

void la_lcdPutChar(uint8_t c)
{
	fprintf(stdout,"D: sending PC%c\n", c);
	serialPrintf(fdsArduino[0], "PC%c\n", c);
	//serialFlush(fdsArduino[0]);	
	usleep(5000);
	//NUGET getchar()
}

static void tr(char* str)
{
	size_t len;
	int i;
	len = strlen(str);
	for(i=0;i<len;i++)
	{
		switch(str[i])
		{
			case (char)224:
			case (char)225:
			case (char)226:
			case (char)227:
			case (char)228:
			case (char)229:
			case (char)230:
				str[i] = 'a';
				break;
			
			case (char)231:
				str[i] = 235;
				break;

			case (char)232:
			case (char)233:
			case (char)235:
				str[i] = 'e';
				break;
		}
	}
}

void la_lcdPuts(char* str)
{
	size_t len;
	char* inbuf = str;
	size_t inbuflen = strlen(str);
	char* output = conv_buf;
	size_t output_len = conv_buf_len;
	size_t stop;
	int i;
	char c;

	len = iconv(conv, &inbuf, &inbuflen, &output, &output_len);
	if(len == -1)
	{
		fprintf(stderr, "E: invalid string to display:%s\n", str);
	}
	else
	{
		*output = '\0';
		stop = 16 - saved_x;
		conv_buf[stop] = '\0';
		tr(conv_buf);
		printf("D: %s|%s\n", str, conv_buf);
		//len = strlen(conv_buf);
		//for(i=0;i<len; i+=9)
		//{
		//if(i>0)
		//{
		//	conv_buf[i] = c;
		//}
			
		//if(i + 9 <len){
		//	c = conv_buf[i+9];
		//	conv_buf[i+9]=0;
		//}
			
		//printf("D: sending PI:PS:%s\n", conv_buf+i);
		//serialPrintf(fdsArduino[0], "PI:PS:%s\n", conv_buf+i);
		//serialFlush(fdsArduino[0]);
		////NUGET getchar()
		// //usleep(10000);
		//}
		//usleep(500000);
		//for(i=0;i<strlen(conv_buf);i++)
		//{
		//	la_lcdPutChar(conv_buf[i]);
		//}
		//usleep(500000);
		printf("D: sending PS%s\n", conv_buf);
		serialPrintf(fdsArduino[0], "PS%s\n", conv_buf);
		//serialFlush(fdsArduino[0]);
		usleep(5000);
	}
	//NUGET getchar()
	
}

void la_ecran_change_state(bool sleep)
{
	//NOOP
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
	}else
	{
		fprintf(stdout, "%s", buf);
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

int main()
{
	int fdControlCount;
	int* fdControls;
	int ret;
	int avail;
	int len;
	
	printf("HELLO WORLD\n");

	int play_ok = 0;
	int i;

	if(la_init_controls(&fdControls, &fdControlCount))
	{
		fprintf(stderr, "E: init controls\n");
		return -1;
	}

	if(la_init_ecran())
	{
		la_lcdHome();
		la_lcdPutChar('S');
		la_lcdPutChar('A');
	}

	while(1)
	{
		avail = serialDataAvail(fdsArduino[0]);
		if(avail > 0)
		{
		la_control_input_one(fdsArduino[0]);
		}
		
		avail = serialDataAvail(0);
		while(avail > 0)
		{
			fprintf(stdout, "GOT %i\n", avail);
			len = getline(&buf, &buf_len, stdin);
			if(len>0)
			{
				fprintf(stdout, "%s",buf);
			}
			serialPuts(fdsArduino[0], buf);
			avail-=len;
		}
		usleep(5000);
	}
	return 0;
}