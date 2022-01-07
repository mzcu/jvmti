OS=$(shell uname -s | tr '[A-Z]' '[a-z]')
ARCH=$(shell uname -p)
CXXFLAGS=-std=c++17
LDFLAGS=-shared
RM=rm -rf
PROTOC=protoc
PROTOBUF=

ifdef PROTOBUF
	LDFLAGS += -lprotobuf
	PROFILE_EXPORT_OBJS = profile.pb.o profile_exporter_pprof.o
else
	PROFILE_EXPORT_OBJS = profile_exporter_flamegraph.o
endif

ifeq ($(OS), darwin)
	JAVA=/Library/Java/JavaVirtualMachines/openjdk.jdk/Contents/Home
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
	JAVA=$(JAVA_HOME)
	CXXFLAGS += -fPIC -I$(JAVA)/include -I$(JAVA)/include/linux
	PREFIX = /usr/local
	CXXFLAGS += -I$(PREFIX)/include
#	statically linking stdlib on linux for easier deployment
	LDFLAGS += -static-libstdc++ -static-libgcc -L$(PREFIX)/lib
	TARGET=libheapz.so
endif


all: Heapz.class $(TARGET)

$(TARGET): $(PROFILE_EXPORT_OBJS) heapz.o
	$(CXX) $(LDFLAGS) -o $@ $^

test:
	$(JAVA)/bin/java -Xmx16m -agentpath:./$(TARGET) -Xint -XX:-Inline SamplingExample

testOneShot:
	$(JAVA)/bin/java -Xmx16m -agentpath:./$(TARGET)=oneshot -Xint -XX:-Inline OneShotExample

buildJava:
	$(JAVA)/bin/javac SamplingExample.java OneShotExample.java

Heapz.class: Heapz.java
	$(JAVA)/bin/javac Heapz.java
	xxd -i $@ > heapz-inl.h

clean:
	$(RM) target/ *.o *.dylib *.so *.prof Heapz.class

release:
	mkdir -p target
	docker build . -t heapz-build
	docker run --rm -it -v $(shell pwd)/target:/opt/heapz/drop heapz-build

protoc:
	$(PROTOC) --cpp_out=. profile.proto
