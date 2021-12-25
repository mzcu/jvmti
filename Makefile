OS=$(shell uname -s | tr '[A-Z]' '[a-z]')
ARCH=$(shell uname -p)
CXXFLAGS=-std=c++20 -DDEBUG
LDFLAGS=-shared
PROTOC=protoc
PROTOBUF=true

ifdef PROTOBUF
	LDFLAGS += -lprotobuf
	PROFILE_EXPORT_OBJS = profile.pb.o profile_exporter_pprof.o
else
$(error Only protobuf export is currently supported, flag PROTOBUF must be set)
endif

ifeq ($(OS), darwin)
	JAVA=/Library/Java/JavaVirtualMachines/openjdk.jdk/Contents/Home
	CXXFLAGS += -I$(JAVA)/include -I$(JAVA)/include/darwin
	ifeq ($(ARCH), arm)
		PREFIX = /opt/homebrew
	else
		PREFIX = /usr/local
	endif
	CXXFLAGS += -I$(PREFIX)/include
	LDFLAGS += -L$(PREFIX)/lib
	TARGET=libheapz.dylib
endif



all: Heapz.class $(TARGET)

$(TARGET): $(PROFILE_EXPORT_OBJS) heapz.o
	$(CXX) $(LDFLAGS) -o $@ $^ -lc

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
	$(RM) *.o *.dylib Heapz.class

protoc:
	$(PROTOC) --cpp_out=. profile.proto
