GTEST_DIR=../googletest/googletest
BINS=CreateTest.o BlockSignalTest.o YieldTest.o SleepTest.o SpinLockTest.o ConditionTest.o SwapContextTest.o JoinTest.o
CXXFLAGS=-std=c++11 -O3 -Wall -Werror -Wformat=2 -Wextra -Wwrite-strings -Wno-unused-parameter -Wmissing-format-attribute -Wno-non-template-friend -Woverloaded-virtual -Wcast-qual -Wcast-align -Wconversion -Weffc++ -fomit-frame-pointer
INCLUDE=-I.. -lArachne -I../../PerfUtils -isystem ${GTEST_DIR}/include
LIBS=-L../ -L../../PerfUtils -L./ -lArachne -lPerfUtils -lgtest -pthread

run: ArachneTest
	./ArachneTest

ArachneTest: $(BINS)
	g++ $(CXXFLAGS) $^ $(GTEST_DIR)/src/gtest_main.cc $(LIBS) -o $@

$(BINS) : %.o : %.cc ../libArachne.a libgtest.a
	g++ -c $(CXXFLAGS) $< $(INCLUDE) -o $@

libgtest.a:
	g++ -isystem ${GTEST_DIR}/include -I${GTEST_DIR} \
		-pthread -c ${GTEST_DIR}/src/gtest-all.cc
		ar -rv libgtest.a gtest-all.o

clean:
	rm -f $(BINS) ArachneTest
