# Compile a static library

DEBUG=-g

libArachne.a: Arachne.o
	ar rcs $@ $<

%.o: %.cc
	g++  -Wall -Werror  -O2 $(DEBUG) -c -std=c++11 -o $@ $<

clean:
	rm -f *.o *.a

Arachne.S: Arachne.cc Arachne.h
	g++ -S -Wall -Werror  -O2 $(DEBUG) -c -std=c++11 -o $@ $<
