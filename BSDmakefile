CC = cc
CFLAGS = -Wall -g -I. -I/usr/local/include -I/usr/local/include/freetype2
CFLAGS += !=pkg-config --cflags x11 xft fontconfig freetype2 gio-2.0 2>/dev/null
LDFLAGS = -L/usr/local/lib
LDFLAGS += !=pkg-config --libs x11 xft fontconfig freetype2 gio-2.0 2>/dev/null
LDFLAGS += -lm

OBJS = jfm.o xdg.o dbus.o theme.o

all: jfm

jfm: $(OBJS)
	$(CC) -o $@ $(OBJS) $(LDFLAGS)

jfm.o: jfm.c jfm.h xdg.h dbus.h
	$(CC) $(CFLAGS) -c jfm.c

xdg.o: xdg.c jfm.h xdg.h
	$(CC) $(CFLAGS) -c xdg.c

dbus.o: dbus.c jfm.h dbus.h xdg.h
	$(CC) $(CFLAGS) -c dbus.c

theme.o: theme.c jfm.h
	$(CC) $(CFLAGS) -c theme.c

clean:
	rm -f jfm $(OBJS)

install: jfm
	install -m 755 jfm /usr/local/bin/jfm
	install -m 644 jfm.desktop /usr/local/share/applications/jfm.desktop
	install -m 644 jfm.png /usr/local/share/icons/hicolor/862x194/apps/jfm.png
	mkdir -p /usr/local/share/dbus-1/services
	install -m 644 org.freedesktop.FileManager1.service /usr/local/share/dbus-1/services/org.freedesktop.FileManager1.service
	-update-desktop-database /usr/local/share/applications >/dev/null 2>&1 || true

uninstall:
	rm -f /usr/local/bin/jfm
	rm -f /usr/local/share/applications/jfm.desktop
	rm -f /usr/local/share/icons/hicolor/862x194/apps/jfm.png
	rm -f /usr/local/share/dbus-1/services/org.freedesktop.FileManager1.service
	-update-desktop-database /usr/local/share/applications >/dev/null 2>&1 || true

.PHONY: all clean install uninstall
