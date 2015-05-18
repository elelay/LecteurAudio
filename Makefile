ARCH:=$(strip $(shell uname -m))
ifeq ($(ARCH),armv6l)
RPI:=TRUE
INC:=-DRPI
LINK:=-lwiringPi -lwiringPiDev
else
INC:=
LINK:=-lncurses
endif

ALL:=la leds_on_off

CFLAGS:=-Wall $(INC) -g
LDFLAGS:=-lmpdclient -lrt $(LINK)

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
