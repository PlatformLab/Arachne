# Compile a static library

DEBUG=-g

LIBS=-I../Arachne  -L../Arachne -lArachne  -I../PerfUtils -L../PerfUtils -lPerfUtils

libArachne.a: Arachne.o Condition.o
	ar rcs $@ $^

Arachne.o: SpinLock.h Arachne.h Condition.h

%.o: %.cc
	g++  -Wall -Werror -fomit-frame-pointer  -O3 $(DEBUG) $(LIBS) -c -std=c++11  -o $@ $<

clean:
	rm -f *.o *.a

test: libArachne.a
	make -C tests
