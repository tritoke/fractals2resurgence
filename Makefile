include config.mk

SRC = ${wildcard *.c}
OBJ = ${SRC:.c=.o}

all: options f2r

options:
	@echo f2g build options:
	@echo "CFLAGS   = ${CFLAGS}"
	@echo "LDFLAGS  = ${LDFLAGS}"
	@echo "CC       = ${CC}"

.c.o:
	${CC} -c ${CFLAGS} $<

${OBJ}: config.h

f2r: ${OBJ}
	${CC} -o $@ ${OBJ} ${LDFLAGS}

clean:
	rm -f f2g ${OBJ}

.PHONY: all options clean
