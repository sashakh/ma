
CFLAGS:= -Wall -g -I. -DMODEM_DEBUG
LDFLAGS:=
LIBS:= -lasound

progs:= mdial mtest mloop
sources:= $(wildcard *.c)
objs:= $(sources:.c=.o)
tables:= m_tables.h cos_table.c v22_tables.c
libs:= libtables.a

m_objs:= m.o modem.o cmdline.o debug.o async.o
drv_objs:= drv_file.o drv_alsa.o
dp_objs:= dialer.o detector.o v21.o v22.o fsk.o psk.o

all: $(libs) $(progs)

$(progs): $(m_objs) $(drv_objs) $(dp_objs) $(libs)

libtables.a: $(tables:.c=.o)

$(tables):
	$(MAKE) -C tools gen_tables
	./tools/gen_tables

$(progs): %: %.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)

$(libs): %.a:
	$(AR) cr $(AFLAGS) $@ $^

$(objs): %.o: %.c
	$(CC) $(CFLAGS) -o $@ -c $<

clean:
	$(RM) $(progs) $(libs) $(objs) *.o
	
clean-all: clean
	$(RM) $(tables) .depend *~
	$(MAKE) -C tools clean

dep:
	$(CC) -M $(CFLAGS) $(sources) > .depend
-include .depend
