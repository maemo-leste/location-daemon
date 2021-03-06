.POSIX:

NAME = location-daemon
DBUS_SERVICE = org.maemo.LocationDaemon.service
SUDOERS = location-daemon.sudoers

PREFIX = /usr
ETCPREFIX = /etc

LIBGPS_CFLAGS = $(shell pkg-config --cflags libgps)
LIBGPS_LIBS = $(shell pkg-config --libs libgps)

DBUS_CFLAGS = $(shell pkg-config --cflags dbus-1 dbus-glib-1)
DBUS_LIBS = $(shell pkg-config --libs dbus-1 dbus-glib-1)

GLIB_CFLAGS = $(shell pkg-config --cflags glib-2.0)
GLIB_LIBS = $(shell pkg-config --libs glib-2.0)

LOCATIONDAEMON_CFLAGS = -O2 -Wall -Wextra -Werror ${DBUS_CFLAGS} ${LIBGPS_CFLAGS} ${GLIB_CFLAGS} ${CFLAGS}
LOCATIONDAEMON_CPPFLAGS = -D_GNU_SOURCE -Wall -pedantic ${CPPFLAGS}
LOCATIONDAEMON_LDFLAGS = ${DBUS_LIBS} ${LIBGPS_LIBS} ${GLIB_LIBS} ${LDFLAGS}

SRC = ${NAME}.c
BIN = ${NAME}
OBJ = ${SRC:.c=.o}

all: location-daemon

.c.o:
	${CC} -c ${LOCATIONDAEMON_CFLAGS} ${LOCATIONDAEMON_CPPFLAGS} $<

${OBJ}:

location-daemon: ${OBJ}
	${CC} -o $@ ${OBJ} ${LOCATIONDAEMON_LDFLAGS}

clean:
	rm -f ${BIN} ${OBJ}

install: all
	mkdir -p ${DESTDIR}${PREFIX}/sbin
	mkdir -p ${DESTDIR}${PREFIX}/share/dbus-1/system-services
	mkdir -p ${DESTDIR}${ETCPREFIX}/sudoers.d
	cp -f ${BIN} ${DESTDIR}${PREFIX}/sbin
	chmod 755 ${DESTDIR}${PREFIX}/sbin/${BIN}
	cp -f ${DBUS_SERVICE} ${DESTDIR}${PREFIX}/share/dbus-1/system-services
	chmod 644 ${DESTDIR}${PREFIX}/share/dbus-1/system-services/${DBUS_SERVICE}
	cp -f ${SUDOERS} ${DESTDIR}${ETCPREFIX}/sudoers.d
	chmod 440 ${DESTDIR}${ETCPREFIX}/sudoers.d/${SUDOERS}

uninstall:
	rm -f ${DESTDIR}${PREFIX}/sbin/${BIN}
	rm -f ${DESTDIR}${PREFIX}/share/dbus-1/system-services/${DBUS_SERVICE}
	rm -f ${DESTDIR}${ETCPREFIX}/sudoers.d/${SUDOERS}

.PHONY: all clean install uninstall
