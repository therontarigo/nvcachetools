
CFLAGS+=-Wall -Werror -Wno-error=unused

CFLAGS+=-I/usr/local/include
LDFLAGS+=-L/usr/local/lib

.MAIN: all
.PHONY: all

all: nvcachedec nvucdump

nvcachedec: nvcachedec.c readfile.c
	${CC} -o $@ nvcachedec.c readfile.c ${CFLAGS} ${LDFLAGS} -lzstd

nvucdump: nvucdump.c readfile.c
	${CC} -o $@ nvucdump.c readfile.c ${CFLAGS} ${LDFLAGS}

