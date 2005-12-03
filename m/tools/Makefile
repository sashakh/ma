TOP_DIR:=..

CFLAGS:= -Wall -g -I. -I..
LDFLAGS:= -lm

#USE_FFTW3:=1
#CFLAGS+= -DUSE_FFTW3
#LDFLAGS+= -lfftw3

progs:= gen_tables tonegen
sources:= $(wildcard *.c)
objs:= $(sources:.c=.o)
tables:= m_tables.h cos_table.c v21_filters.c v22_tables.c

all: $(progs)

gen_tables: filters.o

clean:
	$(RM) $(progs) $(objs) $(tables) *.dat *.o *~

dep:
	$(CC) -M $(CFLAGS) $(sources) > .depend
-include .depend

$(progs): %: %.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)

$(objs): %.o: %.c
	$(CC) $(CFLAGS) -o $@ -c $<

