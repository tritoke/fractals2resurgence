CC = cc
CFLAGS = -pedantic -std=c99 -Wall -Wextra -Ofast -D_DEFAULT_SOURCE
LDFLAGS = -lpthread

SRC = ${wildcard *.c}
OBJ = ${SRC:.c=.o}

all: options f2g

options:
	@echo f2g build options:
	@echo "CFLAGS   = ${CFLAGS}"
	@echo "LDFLAGS  = ${LDFLAGS}"
	@echo "CC       = ${CC}"

.c.o:
	${CC} -c ${CFLAGS} $<

f2g: ${OBJ}
	${CC} -o $@ ${OBJ} ${LDFLAGS}

debug: ${OBJ}
	${CC} ${OBJ} ${CFLAGS} -g3 -OO -o f2g

clean:
	rm -f f2g ${OBJ}

.PHONY: all options clean
