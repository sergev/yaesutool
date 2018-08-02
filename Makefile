CC		= gcc -m64

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

install:	yaesutool yaesutool-ru.mo
		install -c -s yaesutool /usr/local/bin/yaesutool
		install -D yaesutool-ru.mo /usr/local/share/locale/ru/LC_MESSAGES/yaesutool.mo

yaesutool.linux: yaesutool
		cp -p $< $@
		strip $@

yaesutool.po:   $(SRCS)
		xgettext --from-code=utf-8 --keyword=_ \
                    --package-name=YaesuTool --package-version=$(VERSION) \
                    $(SRCS) -o $@

yaesutool-ru.mo: yaesutool-ru.po
		msgfmt -c -o $@ $<

yaesutool-ru-cp866.mo: yaesutool-ru.po
		iconv -f utf-8 -t cp866 $< | sed 's/UTF-8/CP866/' | msgfmt -c -o $@ -

###
ft-60.o: ft-60.c radio.h util.h
main.o: main.c radio.h util.h
radio.o: radio.c radio.h util.h
util.o: util.c util.h
vx-2.o: vx-2.c radio.h util.h
