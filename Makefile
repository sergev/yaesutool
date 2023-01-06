CC		= gcc

VERSION         = 1.0
CFLAGS		= -g -O -Wall -Werror -DVERSION='"$(VERSION)"'
LDFLAGS		=

OBJS		= main.o util.o radio.o ft-60.o vx-2.o
SRCS		= main.c util.c radio.c ft-60.c vx-2.c
LIBS            =

# Mac OS X
#CFLAGS          += -I/usr/local/opt/gettext/include
#LIBS            += -L/usr/local/opt/gettext/lib -lintl

all:		yaesutool

yaesutool:	$(OBJS)
		$(CC) $(LDFLAGS) -o $@ $(OBJS) $(LIBS)

clean:
		rm -f *~ *.o core yaesutool

install:	yaesutool
		install -c -s yaesutool /usr/local/bin/yaesutool

yaesutool.linux: yaesutool
		cp -p $< $@
		strip $@

###
ft-60.o: ft-60.c radio.h util.h
main.o: main.c radio.h util.h
radio.o: radio.c radio.h util.h
util.o: util.c util.h
vx-2.o: vx-2.c radio.h util.h
