LIBPTHREAD = -lpthread
LIBS = ${LIBPTHREAD}

CPPFLAGS = -D_DEFAULT_SOURCE -D_BSD_SOURCE -D_POSIX_C_SOURCE=2
CFLAGS = -std=c99 -pedantic -Wall -Wno-deprecated-declarations -Ofast ${CPPFLAGS}
LDFLAGS = ${LIBS}

CC = cc
