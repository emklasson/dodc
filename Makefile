CC=clang++
CPPFLAGS=-std=c++23 -Wall
LDFLAGS=

all: dodc schedule_bg

dodc: dodc.o dodc_gmp_ecm.o dodc_msieve.o dodc_cado_nfs.o multiprocessing.o
	$(CC) $(LDFLAGS) $^ -o $@

schedule_bg: schedule_bg.o multiprocessing.o
	$(CC) $(CPPFLAGS) $^ -o $@

%.o: %.cc
	$(CC) $(CPPFLAGS) -c $<

clean:
	rm -f *.o
