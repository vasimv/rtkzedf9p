CC=gcc
LIBS=-lm

all: rtkzedf9p

install: all
	install -c -m 0555 rtkzedf9p /usr/local/bin/rtkzedf9p

rtkzedf9p.o: rtkzedf9p.cpp TinyGPS++.h
	$(CC) -c -g -o $@ rtkzedf9p.cpp

TinyGPS++.o: TinyGPS++.cpp TinyGPS++.h
	$(CC) -c -g -o $@ TinyGPS++.cpp

rtkzedf9p: rtkzedf9p.o TinyGPS++.o
	$(CC) -g -o $@ rtkzedf9p.o TinyGPS++.o $(LIBS) 

clean:
	rm -f *.o rtkzedf9p
