CC=g++
CCFLAGS=-g -Wall -Werror -Wformat=2 -Wextra -Wwrite-strings \
-Wno-unused-parameter -Wmissing-format-attribute -Wno-non-template-friend \
-Woverloaded-virtual -Wcast-qual -Wcast-align -Wconversion -fomit-frame-pointer \
-std=c++11 -fPIC -O3

# Output directories
OBJECT_DIR = obj
SRC_DIR = src
INCLUDE_DIR = include
LIB_DIR = lib
BIN_DIR = bin

# Depenencies
PERFUTILS=../PerfUtils
COREARBITER=../CoreArbiter
INCLUDE=-I$(PERFUTILS)/include -I$(COREARBITER)/include
LIBS=$(PERFUTILS)/lib/libPerfUtils.a $(COREARBITER)/lib/libCoreArbiter.a -pthread

# Stuff needed for make check
TOP := $(shell echo $${PWD-`pwd`})
ifndef CHECK_TARGET
CHECK_TARGET=$$(find $(SRC_DIR) '(' -name '*.h' -or -name '*.cc' ')' -not -path '$(TOP)/googletest/*' )
endif

# Conversion to fully qualified names
OBJECT_NAMES := Arachne.o Logger.o PerfStats.o

OBJECTS = $(patsubst %,$(OBJECT_DIR)/%,$(OBJECT_NAMES))
HEADERS= $(shell find src -name '*.h')


install: $(OBJECT_DIR)/libArachne.a
	mkdir -p $(LIB_DIR) $(INCLUDE_DIR)
	cp $(HEADERS) $(INCLUDE_DIR)
	cp $< $(LIB_DIR)

$(OBJECT_DIR)/libArachne.a: $(OBJECTS)
	ar rcs $@ $^

$(OBJECT_DIR)/%.o: $(SRC_DIR)/%.cc $(HEADERS) | $(OBJECT_DIR)
	$(CC) $(INCLUDE) $(CCFLAGS) -c $< -o $@

$(OBJECT_DIR):
	mkdir -p $(OBJECT_DIR)

check:
	scripts/cpplint.py --filter=-runtime/threadsafe_fn,-readability/streams,-whitespace/blank_line,-whitespace/braces,-whitespace/comments,-runtime/arrays,-build/include_what_you_use,-whitespace/semicolon $(CHECK_TARGET)
	! grep '.\{81\}' *.h *.cc

################################################################################
# Test Targets
GTEST_DIR=../googletest/googletest
TEST_LIBS=-Lobj/ -lArachne $(OBJECT_DIR)/libgtest.a
INCLUDE+=-I${GTEST_DIR}/include

test: $(OBJECT_DIR)/ArachneTest
	$(OBJECT_DIR)/ArachneTest

$(OBJECT_DIR)/ArachneTest: $(OBJECT_DIR)/ArachneTest.o $(OBJECT_DIR)/libgtest.a $(OBJECT_DIR)/libArachne.a
	$(CC) $(INCLUDE) $(CCFLAGS) $< $(GTEST_DIR)/src/gtest_main.cc $(TEST_LIBS) $(LIBS)  -o $@

$(OBJECT_DIR)/libgtest.a:
	g++ -I${GTEST_DIR}/include -I${GTEST_DIR} \
	-pthread -c ${GTEST_DIR}/src/gtest-all.cc \
	-o $(OBJECT_DIR)/gtest-all.o
	ar -rv $(OBJECT_DIR)/libgtest.a $(OBJECT_DIR)/gtest-all.o

################################################################################
# Doc targets

docs:
	doxygen Doxyfile

site: docs
	git fetch origin gh-pages:gh-pages
	scripts/git-replace-branch html gh-pages "Updating website on $(shell date) from commit $(shell git rev-parse HEAD)."
	git push origin gh-pages

clean:
	rm -rf $(OBJECT_DIR) $(LIB_DIR)
