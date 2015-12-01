# Compile a static library

DEBUG=-g

libArachne.a: Arachne.o
	ar rcs $@ $<

%.o: %.cc
	g++ $(DEBUG) -c -std=c++11 -o $@ $<

clean:
	rm -f *.o *.a
