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

int main()
{
	int ret;
	int fd;

	ret = wiringPiSetup();
	if(ret) return ret;

	if ((fd = serialOpen ("/dev/ttyACM0", 9600)) < 0)
    {
    	fprintf (stderr, "E: Unable to open serial device: %s\n", strerror (errno)) ;
    	return -1;
    }

	return 0;
}