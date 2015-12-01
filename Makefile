# Compile a static library
#
libArachne.a: Arachne.o
	ar rcs $@ $<

%.o: %.cc
	g++ -c -std=c++11 -o $@ $<

clean:
	rm -f *.o *.a
