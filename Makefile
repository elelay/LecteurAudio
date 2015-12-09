ARCH:=$(strip $(shell uname -m))
ifeq ($(ARCH),armv6l)
RPI:=TRUE
# alarpi
# INC:=-DRPI
# raspbian
INC:=-DRPI -I/opt/libmpdclient210/include
LINK:=-lwiringPi -lwiringPiDev
ALL:=la leds_on_off sb
else
INC:=
LINK:=-lncurses
ALL:=la gpodder_test
endif


CFLAGS:=-Wall $(INC) -g
# alarmpi
# LDFLAGS:=-lmpdclient -lrt $(LINK)
# raspbian
LDFLAGS:=-L/opt/libmpdclient210/lib -Wl,-rpath -Wl,/opt/libmpdclient210/lib -lmpdclient -lrt -lcurl $(LINK)
LDFLAGS_LIGHT:= -lwiringPi -lwiringPiDev

.PHONY: all clean

all: $(ALL)

ifeq ($(strip $(RPI)),)
la: emul.o
else
la: magneto_arduino_serial.o
endif

la: main.o controles.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(filter-out main.o,$(filter-out %.h,$^)) main.o

main.o: controles.h ecran.h

leds_on_off: leds_on_off.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $<

sb: serial_bridge.o
	$(CC) $(CFLAGS) $(LDFLAGS_LIGHT) -o $@ $<

gpodder.o gpodder_test: CFLAGS := $(CFLAGS) -Ideps/jsmn

gpodder_test: gpodder_test.o gpodder.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(filter-out gpodder_test.o,$(filter-out %.h,$^)) deps/jsmn/libjsmn.a gpodder_test.o

clean:
	rm -f la *.o

grind:
	valgrind --log-file=grind.log ./la
