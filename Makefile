include config.mk

SRC = f2r.c
OBJ = ${SRC:.c=.o}

all: options f2r

options:
	@echo f2r build options:
	@echo "CFLAGS   = ${CFLAGS}"
	@echo "LDFLAGS  = ${LDFLAGS}"
	@echo "CC       = ${CC}"

.c.o:
	${CC} -c ${CFLAGS} $<

${OBJ}: defaults.h config.mk ${CMAPINC}/cmap.h

f2r: ${OBJ} ${CMAPINC}/libcmap.a
	${CC} -o $@ ${OBJ} ${LDFLAGS}

clean:
	rm -f f2r ${OBJ} feh_*.ff

.PHONY: all options clean
