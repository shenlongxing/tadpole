# ARCH
uname_S := $(shell sh -c 'uname -s 2>/dev/null || echo not')

XXDB_TARGET=tadpole
XXDB_OBJ=db.o skiplist.o commands.o zmalloc.o \
					 dict.o sds.o config.o anet.o util.o  \
					 log.o setproctitle.o

DEBUG=-g -ggdb
CFLAGS+=-Wall -DHAVE_EPOLL ${DEBUG} -D_GNU_SOURCE -D HAVE_EPOLL -I ae -I./hiredis -lpthread
LIBS=ae/libae.a hiredis/libhiredis.a
LDFLAGS+=-lpthread
CC=gcc
SUBDIRS := $(wildcard */.)
DEPEND=.depend

all:depend build $(XXDB_TARGET)

build:
ifneq ($(uname_S),Linux)
	@echo "Only Linux is supported"
	@exit 1
endif

test:$(XXDB_TARGET)
	@(cd test; ./runtest.sh)

$(XXDB_TARGET): $(XXDB_OBJ) $(LIBS)
	$(CC) -o $@ $^ $(LDFLAGS)

$(LIBS):
	for dir in ${SUBDIRS}; do	\
		${MAKE} static -C $${dir};	\
	done

depend: $(DEPEND)

$(DEPEND): *.c
	@rm -f ./.depend
	$(CC) $(CFLAGS) -MM $^ > ./$(DEPEND);

include $(DEPEND)

clean:
	for dir in ${SUBDIRS}; do	\
		${MAKE} clean -C $${dir};	\
	done
	rm -rf $(XXDB_TARGET) $(XXDB_OBJ)   $(DEPEND)

.PHONY: clean
