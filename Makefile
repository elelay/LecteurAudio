ARCH:=$(strip $(shell uname -m))
ifeq ($(ARCH),armv6l)
RPI:=TRUE
# alarpi
# INC:=-DRPI
# raspbian
INC:=-DRPI -I/opt/libmpdclient210/include
LINK:=-lwiringPi -lwiringPiDev -lcurl
else
INC:=
LINK:=-lncurses -lcurl
endif

ALL:=la leds_on_off

CFLAGS:=-Wall $(INC) -g
# alarmpi
# LDFLAGS:=-lmpdclient -lrt $(LINK)
# raspbian
LDFLAGS:=-L/opt/libmpdclient210/lib -Wl,-rpath -Wl,/opt/libmpdclient210/lib -lmpdclient -lrt $(LINK)

.PHONY: all clean

all: $(ALL)

ifeq ($(strip $(RPI)),)
la: emul.o
else
la: lcd.o magneto_arduino.o
endif

la: main.o controles.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(filter-out main.o,$(filter-out %.h,$^)) main.o

main.o: controles.h ecran.h

leds_on_off: leds_on_off.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $<

clean:
	rm -f la *.o

grind:
	valgrind --log-file=grind.log ./la
