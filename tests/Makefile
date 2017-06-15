GTEST_DIR=../googletest/googletest
BINS=ArachneTest.o
CXXFLAGS=-std=c++11 -O3 -Wall -Werror -Wformat=2 -Wextra -Wwrite-strings -Wno-unused-parameter -Wmissing-format-attribute -Wno-non-template-friend -Woverloaded-virtual -Wcast-qual -Wcast-align -Wconversion -fomit-frame-pointer -g
INCLUDE=-I.. -lArachne -I../PerfUtils -I${GTEST_DIR}/include
LIBS=-L.. -L. -lArachne  -lgtest -pthread -L../CoreArbiter -lCoreArbiter ../PerfUtils/libPerfUtils.a

run: ArachneTest
	./ArachneTest

ArachneTest: $(BINS)
	g++ $(CXXFLAGS) $^ $(LIBS) $(GTEST_DIR)/src/gtest_main.cc  -o $@

$(BINS) : %.o : %.cc ../libArachne.a libgtest.a
	g++ -c $(CXXFLAGS) $< $(INCLUDE) -o $@

libgtest.a:
	g++ -I${GTEST_DIR}/include -I${GTEST_DIR} \
		-pthread -c ${GTEST_DIR}/src/gtest-all.cc
		ar -rv libgtest.a gtest-all.o

clean:
	rm -f $(BINS) ArachneTest
