PREFIX    ?= $(DESTDIR)/usr/local
MANPREFIX ?= $(PREFIX)/man
CFLAGS += -Wall -Wextra
LDLIBS += -lcurses

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

.PHONY: all clean install uninstall
