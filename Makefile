CXX ?= g++
CC ?= gcc
CXXFLAGS=-g -Wall -Werror -Wformat=2 -Wextra -Wwrite-strings \
-Wno-unused-parameter -Wmissing-format-attribute -Wno-non-template-friend \
-Woverloaded-virtual -Wcast-qual -Wcast-align -fomit-frame-pointer \
-std=c++11 -fPIC -O3 $(EXTRA_CXXFLAGS)
CFLAGS=-g -Wall -Werror -Wformat=2 -Wextra -Wwrite-strings \
-Wno-unused-parameter -Wmissing-format-attribute \
-Wcast-align -Wconversion -fomit-frame-pointer \
-std=gnu99 -fPIC -O3

# Output directories
OBJECT_DIR = obj
SRC_DIR = src
WRAPPER_DIR = cwrapper
INCLUDE_DIR = include/Arachne
LIB_DIR = lib
BIN_DIR = bin

# Depenencies
PERFUTILS=../PerfUtils
COREARBITER=../CoreArbiter
INCLUDE=-I$(PERFUTILS)/include -I$(COREARBITER)/include -I$(SRC_DIR)
LIBS=$(COREARBITER)/lib/libCoreArbiter.a $(PERFUTILS)/lib/libPerfUtils.a -lpcrecpp -pthread
CLIBS=$(LIBS) -lstdc++

# Stuff needed for make check
TOP := $(shell echo $${PWD-`pwd`})
ifndef CHECK_TARGET
CHECK_TARGET=$$(find $(SRC_DIR) $(WRAPPER_DIR) '(' -name '*.h' -or -name '*.cc' ')' -not -path '$(TOP)/googletest/*' )
endif

# Conversion to fully qualified names
OBJECT_NAMES := Arachne.o Logger.o PerfStats.o DefaultCorePolicy.o CoreLoadEstimator.o arachne_wrapper.o

OBJECTS = $(patsubst %,$(OBJECT_DIR)/%,$(OBJECT_NAMES))
HEADERS= $(shell find $(SRC_DIR) $(WRAPPER_DIR) -name '*.h')

ifeq ($(MAKECMDGOALS),clean)
DEP=
else
DEP=$(OBJECTS:.o=.d)
endif # ($(MAKECMDGOALS),clean)

install: $(OBJECT_DIR)/libArachne.a
	mkdir -p $(LIB_DIR) $(INCLUDE_DIR)
	cp $(HEADERS) $(INCLUDE_DIR)
	cp $< $(LIB_DIR)

$(OBJECT_DIR)/libArachne.a: $(OBJECTS)
	ar rcs $@ $^

-include $(DEP)

$(OBJECT_DIR)/%.d: $(WRAPPER_DIR)/%.c | $(OBJECT_DIR)
	$(CC) $(INCLUDE) $(CFLAGS) $< -MM -MT $(@:.d=.o) > $@

$(OBJECT_DIR)/%.o: $(WRAPPER_DIR)/%.c $(HEADERS) | $(OBJECT_DIR)
	$(CC) $(INCLUDE) $(CFLAGS) -c $< -o $@

$(OBJECT_DIR)/%.d: $(WRAPPER_DIR)/%.cc | $(OBJECT_DIR)
	$(CXX) $(INCLUDE) $(CXXFLAGS) $< -MM -MT $(@:.d=.o) > $@

$(OBJECT_DIR)/%.o: $(WRAPPER_DIR)/%.cc $(HEADERS) | $(OBJECT_DIR)
	$(CXX) $(INCLUDE) $(CXXFLAGS) -c $< -o $@

$(OBJECT_DIR)/%.d: $(SRC_DIR)/%.cc | $(OBJECT_DIR)
	$(CXX) $(INCLUDE) $(CXXFLAGS) $< -MM -MT $(@:.d=.o) > $@

$(OBJECT_DIR)/%.o: $(SRC_DIR)/%.cc $(HEADERS) | $(OBJECT_DIR)
	$(CXX) $(INCLUDE) $(CXXFLAGS) -c $< -o $@

$(OBJECT_DIR):
	mkdir -p $(OBJECT_DIR)

check:
	scripts/cpplint.py --filter=-runtime/threadsafe_fn,-readability/streams,-whitespace/blank_line,-whitespace/braces,-whitespace/comments,-runtime/arrays,-build/include_what_you_use,-whitespace/semicolon $(CHECK_TARGET)
	! grep '.\{81\}' $(SRC_DIR)/*.h $(SRC_DIR)/*.cc $(WRAPPER_DIR)/*.h $(WRAPPER_DIR)/*.cc

################################################################################
# Test Targets
GTEST_DIR=../googletest/googletest
GMOCK_DIR=../googletest/googlemock
TEST_LIBS=-Lobj/ -lArachne $(OBJECT_DIR)/libgtest.a
CTEST_LIBS=-Lobj/ -lArachne
INCLUDE+=-I${GTEST_DIR}/include -I${GMOCK_DIR}/include
COREARBITER_BIN=$(COREARBITER)/bin/coreArbiterServer

test: $(OBJECT_DIR)/ArachneTest $(OBJECT_DIR)/CorePolicyTest $(OBJECT_DIR)/DefaultCorePolicyTest $(OBJECT_DIR)/arachne_wrapper_test
	$(OBJECT_DIR)/ArachneTest
	$(OBJECT_DIR)/DefaultCorePolicyTest
	$(OBJECT_DIR)/arachne_wrapper_test
	$(OBJECT_DIR)/CorePolicyTest

ctest: $(OBJECT_DIR)/arachne_wrapper_ctest
	$(OBJECT_DIR)/arachne_wrapper_ctest

$(OBJECT_DIR)/arachne_wrapper_ctest: $(OBJECT_DIR)/arachne_wrapper_ctest.o $(OBJECT_DIR)/libArachne.a
	$(CC) $(INCLUDE) $(CFLAGS) $< $(CTEST_LIBS) $(CLIBS)  -o $@

$(OBJECT_DIR)/arachne_wrapper_test: $(OBJECT_DIR)/arachne_wrapper_test.o $(OBJECT_DIR)/libgtest.a $(OBJECT_DIR)/libArachne.a
	$(CXX) $(INCLUDE) $(CXXFLAGS) $< $(GTEST_DIR)/src/gtest_main.cc $(TEST_LIBS) $(LIBS)  -o $@

$(OBJECT_DIR)/ArachneTest: $(OBJECT_DIR)/ArachneTest.o $(OBJECT_DIR)/libgtest.a $(OBJECT_DIR)/libArachne.a
	$(CXX) $(INCLUDE) $(CXXFLAGS) $< $(GTEST_DIR)/src/gtest_main.cc $(TEST_LIBS) $(LIBS)  -o $@

$(OBJECT_DIR)/DefaultCorePolicyTest: $(OBJECT_DIR)/DefaultCorePolicyTest.o $(OBJECT_DIR)/libgtest.a $(OBJECT_DIR)/libArachne.a
	$(CXX) $(INCLUDE) $(CXXFLAGS) $< $(GTEST_DIR)/src/gtest_main.cc $(TEST_LIBS) $(LIBS)  -o $@

$(OBJECT_DIR)/CorePolicyTest: $(OBJECT_DIR)/CorePolicyTest.o $(OBJECT_DIR)/libgtest.a $(OBJECT_DIR)/libArachne.a
	$(CXX) $(INCLUDE) $(CXXFLAGS) $< $(GTEST_DIR)/src/gtest_main.cc $(TEST_LIBS) $(LIBS)  -o $@

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
	rm -rf $(OBJECT_DIR) $(LIB_DIR) $(INCLUDE_DIR)
