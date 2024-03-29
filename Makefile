OS=$(shell uname -s | tr '[A-Z]' '[a-z]')
ARCH=$(shell uname -p)
CXXFLAGS=-std=c++17
LDFLAGS=-shared
RM=rm -rf
PROTOC=protoc
JAVA=$(JAVA_HOME)
PROTOBUF=

ifdef PROTOBUF
	LDFLAGS += -lprotobuf
	PROFILE_EXPORT_OBJS = profile.pb.o profile_exporter_pprof.o
else
	PROFILE_EXPORT_OBJS = profile_exporter_flamegraph.o
endif

ifeq ($(OS), darwin)
	CXXFLAGS += -I$(JAVA)/include -I$(JAVA)/include/darwin -DDEBUG
	ifeq ($(ARCH), arm)
		PREFIX = /opt/homebrew
	else
		PREFIX = /usr/local
	endif
	CXXFLAGS += -I$(PREFIX)/include
	LDFLAGS += -L$(PREFIX)/lib
	TARGET=libheapz.dylib
endif

ifeq ($(OS), linux)
	CXXFLAGS += -fPIC -I$(JAVA)/include -I$(JAVA)/include/linux
	PREFIX = /usr/local
	CXXFLAGS += -I$(PREFIX)/include
#	statically linking stdlib on linux for easier deployment
	LDFLAGS += -static-libstdc++ -static-libgcc -L$(PREFIX)/lib
	TARGET=libheapz.so
endif


all: Heapz.class $(TARGET) runUnitTest

$(TARGET): $(PROFILE_EXPORT_OBJS) heapz.o
	$(CXX) $(LDFLAGS) -o $@ $^

test:
	$(JAVA)/bin/java -Xmx16m -agentpath:./$(TARGET)=interval_bytes=0,max_samples=200000 -Xint -XX:-Inline SamplingExample

testOneShot:
	$(JAVA)/bin/java -Xmx16m -agentpath:./$(TARGET)=oneshot -Xint -XX:-Inline OneShotExample

buildJava:
	$(JAVA)/bin/javac SamplingExample.java OneShotExample.java

Heapz.class: Heapz.java
	$(JAVA)/bin/javac Heapz.java
	xxd -i $@ > heapz-inl.h

clean:
	$(RM) target/ *.o *.dylib *.so *.prof Heapz.class unittest

release:
	mkdir -p target
	docker build . -t heapz-build
	docker run --rm -it -v $(shell pwd)/target:/opt/heapz/drop heapz-build

protoc:
	$(PROTOC) --cpp_out=. profile.proto

GTEST_DIR = third_party/googletest
GTEST_LIB = $(GTEST_DIR)/build/lib
TESTS = storage_test.cc heapz_test.cc

# requires building third_party/googletest
unittest: $(TESTS)
	$(CXX) $(CXXFLAGS) -I$(GTEST_DIR)/googletest/include $(TESTS) -L$(GTEST_LIB) -lpthread -lgtest -lgtest_main -o unittest

runUnitTest: unittest
	./unittest

.PHONY: runUnitTest
