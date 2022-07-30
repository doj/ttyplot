PREFIX    ?= $(DESTDIR)/usr/local
MANPREFIX ?= $(PREFIX)/man
CXXFLAGS  += -Wall -Wextra -O2 -std=c++11
ifeq ($(shell uname),Linux)
LDLIBS += -lncurses -ltinfo
endif
ifeq ($(shell uname),Darwin)
LDLIBS += -lcurses
endif

all: ttyplot

install: ttyplot ttyplot.1
	install -d $(PREFIX)/bin
	install -d $(MANPREFIX)/man1
	install -m755 ttyplot   $(PREFIX)/bin
	install -m644 ttyplot.1 $(MANPREFIX)/man1

uninstall:
	$(RM) -f $(PREFIX)/bin/ttyplot
	$(RM) -f $(MANPREFIX)/man1/ttyplot.1

clean:
	$(RM) -f ttyplot *~

test:	all
	#perl test.pl -2 | ./ttyplot -2 -c AB -t "test title" -u "cm"
	#perl test.pl | ./ttyplot -t "test title" -u "cm"
	#perl test.pl -k | ./ttyplot -k -t "test title" -u "cm" -C 'red blue green'
	perl test.pl -r | ./ttyplot -r -k -b -t "test rate"
	#perl test.pl --rateoverflow | ./ttyplot -r -t "test rate overflow"

.PHONY: all clean install uninstall test
