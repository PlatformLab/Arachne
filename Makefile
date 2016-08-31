# Compile a static library

DEBUG=-g

LIBS=-I../Arachne  -L../Arachne -lArachne  -I../PerfUtils -L../PerfUtils -lPerfUtils

libArachne.a: Arachne.o Condition.o
	ar rcs $@ $^

Arachne.o: SpinLock.h Arachne.h Condition.h

%.o: %.cc
	g++  -Wall -Werror -fomit-frame-pointer -O3 $(DEBUG) $(LIBS) -c -std=c++11  -o $@ $<

clean:
	rm -f *.o *.a

Arachne.S: Arachne.cc Arachne.h
	g++ -S -Wall -Werror  -O2 $(DEBUG) -c -std=c++11 -o $@ $<

TestSpinLock: TestSpinLock.cc
	g++  -Wall -Werror  -O2  $< $(DEBUG) $(LIBS) -std=c++11  -o $@
