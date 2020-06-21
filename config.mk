CMAPINC = libcmap
INCS = -I${CMAPINC}

LIBCMAP = -L${CMAPINC} -lcmap
LIBPTHREAD = -lpthread
LIBS = ${LIBPTHREAD} ${LIBCMAP}

CPPFLAGS = -D_DEFAULT_SOURCE -D_BSD_SOURCE -D_POSIX_C_SOURCE=2
CFLAGS = -std=c99 -pedantic -Wall -Wno-deprecated-declarations -static -Ofast ${INCS} ${CPPFLAGS}
LDFLAGS = ${LIBS}

CC = cc
