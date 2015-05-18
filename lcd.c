#include "ecran.h"

#include <errno.h>
#include <locale.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <iconv.h> 

#include <wiringPi.h>
#include <pcf8574.h>
#include <lcd.h>

// the handle to the LCD
static int lcdHandle;

static iconv_t conv;
static const size_t conv_buf_len = 64;
static char conv_buf[64];
static int saved_x = 0, saved_y = 0;

int la_init_ecran()
{

	setlocale (LC_ALL, "");

  	conv = iconv_open("ISO-8859-1", "UTF-8");
  	if(conv == (iconv_t)(-1))
  	{
  		fprintf(stderr, "E: iconv init failed: %s\n", strerror(errno));
  	}

	lcdHandle = lcdInit (2, 16, 4, 0, 2, 4,5,6,7,0,0,0,0) ;
	if (lcdHandle < 0)
	{
		fprintf(stderr, "E: lcdInit failed\n");
		return -1;
	}
	lcdPosition (lcdHandle, 0, 0);
	la_lcdPuts ("LecteurAudio");

	// int i;
	// unsigned char buf[8] = {0};
	// for(i=126;i<162;i++){
	// 	la_lcdHome();
	// 	lcdClear(lcdHandle);
	// 	buf[0] = i;
	// 	sprintf(buf+1,"%u", i);
	// 	lcdPuts(lcdHandle, buf);
	// 	getchar();
	// }
	
	return 0;
}

void la_lcdHome()
{
	lcdPosition(lcdHandle, 0, 0);
}

void la_lcdClear()
{
	lcdClear(lcdHandle);
}

void la_lcdPosition(int col, int row)
{
	lcdPosition(lcdHandle, col, row);
	saved_x = col;
	saved_y = row;
}

void la_lcdPutChar(uint8_t c)
{
	lcdPutchar(lcdHandle, c);
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
		lcdPuts(lcdHandle, conv_buf);
	}
}

int la_leds_off();
int la_leds_on();

void la_ecran_change_state(bool sleep)
{
	if(sleep)
	{
		lcdDisplay(lcdHandle, 0);
		la_leds_off();
	}
	else
	{
		lcdDisplay(lcdHandle, 1);
		la_leds_on();
	}
}
