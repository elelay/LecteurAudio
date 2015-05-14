ARCH:=$(strip $(shell uname -m))
ifeq ($(ARCH),armv6l)
RPI:=TRUE
INC:=-DRPI
LINK:=-lwiringPi
else
INC:=
LINK:=-lncurses
endif

ALL:=la

CFLAGS:=-Wall $(INC)
LDFLAGS:=-lmpdclient $(LINK)

.PHONY: all clean

all: $(ALL)

ifeq ($(strip $(RPI)),)
la: emul.o
endif

la: main.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(filter-out main.o,$(filter-out %.h,$^)) main.o

main.o: controles.h ecran.h

clean:
	rm -f la *.o