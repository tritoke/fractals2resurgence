CMAPINC = libcmap
INCS = -I${CMAPINC}

LIBCMAP = -L${CMAPINC} -lcmap
LIBPTHREAD = -lpthread
LIBMATH = -lm
LIBS = ${LIBPTHREAD} ${LIBCMAP} ${LIBMATH}

CPPFLAGS = -D_DEFAULT_SOURCE -D_BSD_SOURCE -D_POSIX_C_SOURCE=2 -DMAPDIR="\"$(shell pwd)/${CMAPINC}/colourmaps\""
CFLAGS = -std=c11 -pedantic -Wall -Warray-bounds -Wno-deprecated-declarations -Ofast ${INCS} ${CPPFLAGS} -ggdb -Og
LDFLAGS = ${LIBS}

CC = cc
