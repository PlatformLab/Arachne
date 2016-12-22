# Compile a static library

DEBUG=-g
LIBS=-I../Arachne -L../Arachne -lArachne -L../PerfUtils -lPerfUtils
TOP := $(shell echo $${PWD-`pwd`})

ifndef CHECK_TARGET
CHECK_TARGET=$$(find $(TOP) '(' -name '*.h' -or -name '*.cc' ')' -not -path '$(TOP)/googletest/*' )
endif

CCFLAGS=-Wall -Werror -Wformat=2 -Wextra -Wwrite-strings -Wno-unused-parameter -Wmissing-format-attribute -Wno-non-template-friend -Woverloaded-virtual -Wcast-qual -Wcast-align -Wconversion -fomit-frame-pointer

libArachne.a: Arachne.o
	ar rcs $@ $^

Arachne.o:  Arachne.h

%.o: %.cc
	g++ $(CCFLAGS)  -O3 $(DEBUG) $(LIBS) -fPIC -c -std=c++11 -o $@ $<

test: libArachne.a
	make -C tests

check:
	./cpplint.py --filter=-runtime/threadsafe_fn,-readability/streams,-whitespace/blank_line,-whitespace/braces,-whitespace/comments,-runtime/arrays,-build/include_what_you_use,-whitespace/semicolon $(CHECK_TARGET)
	! grep '.\{81\}' *.h *.cc

docs:
	doxygen Doxyfile

site: docs
	git fetch origin gh-pages:gh-pages
	./git-replace-branch html gh-pages "Updating website on $(shell date) from commit $(shell git rev-parse HEAD)."
	git push origin gh-pages

clean:
	rm -f *.o *.a
