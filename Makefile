.POSIX:

NAME = location-daemon

PREFIX = /usr

LIBGPS_CFLAGS = $(shell pkg-config --cflags libgps)
LIBGPS_LIBS = $(shell pkg-config --libs libgps)

DBUS_CFLAGS = $(shell pkg-config --cflags dbus-1)
DBUS_LIBS = $(shell pkg-config --libs dbus-1)

LOCATIONDAEMON_CFLAGS = ${CFLAGS} ${LIBGPS_CFLAGS} ${DBUS_CFLAGS}
LOCATIONDAEMON_CPPFLAGS = ${CPPFLAGS} -Wall -pedantic
LOCATIONDAEMON_LDFLAGS = ${LDFLAGS} ${LIBGPS_LIBS} ${DBUS_LIBS}

SRC = ${NAME}.c
BIN = ${NAME}
OBJ = ${SRC:.c=.o}

all: location-daemon

options:
	@echo ${NAME} build options:
	@echo "CFLAGS   = ${LOCATIONDAEMON_CFLAGS}"
	@echo "LDFLAGS  = ${LOCATIONDAEMON_LDFLAGS}"
	@echo "CC       = ${CC}"

.c.o:
	${CC} -c ${LOCATIONDAEMON_CFLAGS} ${LOCATIONDAEMON_CPPFLAGS} $<

${OBJ}:

location-daemon: ${OBJ}
	${CC} -o $@ ${OBJ} ${LOCATIONDAEMON_LDFLAGS}

clean:
	rm -f ${BIN} ${OBJ}

install: all
	mkdir -p ${DESTDIR}${PREFIX}/bin
	cp -f ${BIN} ${DESTDIR}${PREFIX}/bin
	chmod 755 ${DESTDIR}${PREFIX}/bin/${BIN}

uninstall:
	rm -f ${DESTDIR}${PREFIX}/bin/${BIN}

.PHONY: all options clean install uninstall
