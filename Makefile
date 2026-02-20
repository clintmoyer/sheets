include config.mk

SRC = sheets.c eval.c util.c
OBJ = $(SRC:.c=.o)

all: sheets

.c.o:
	$(CC) -c $(CFLAGS) $<

config.h:
	cp config.def.h $@

$(OBJ): config.h config.mk util.h eval.h

sheets: $(OBJ)
	$(CC) -o $@ $(OBJ) $(LDFLAGS)

clean:
	rm -f sheets $(OBJ) sheets-$(VERSION).tar.gz

dist: clean
	mkdir -p sheets-$(VERSION)
	cp LICENSE Makefile README arg.h config.def.h config.mk eval.h\
		util.h $(SRC)\
		sheets-$(VERSION)
	tar -cf sheets-$(VERSION).tar sheets-$(VERSION)
	gzip sheets-$(VERSION).tar
	rm -rf sheets-$(VERSION)

install: all
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp -f sheets $(DESTDIR)$(PREFIX)/bin
	chmod 755 $(DESTDIR)$(PREFIX)/bin/sheets
	mkdir -p $(DESTDIR)$(MANPREFIX)/man1
	cp -f sheets.1 $(DESTDIR)$(MANPREFIX)/man1
	chmod 644 $(DESTDIR)$(MANPREFIX)/man1/sheets.1

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/sheets
	rm -f $(DESTDIR)$(MANPREFIX)/man1/sheets.1

.PHONY: all clean dist install uninstall
