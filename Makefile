OS=$(shell uname -s | tr '[A-Z]' '[a-z]')
ARCH=$(shell uname -p)
CXXFLAGS=-std=c++20 -DDEBUG
LDFLAGS=-shared -lprotobuf
PROTOC=protoc

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

$(TARGET): profile.pb.o heapz.o
	$(CXX) $(LDFLAGS) -o $@ $^ -lc

test:
	$(JAVA)/bin/java -Xmx16m -agentpath:./$(TARGET) -Xint -XX:-Inline SamplingExample

buildJava:
	$(JAVA)/bin/javac SamplingExample.java

Heapz.class: Heapz.java
	$(JAVA)/bin/javac Heapz.java
	xxd -i $@ > heapz-inl.h

clean:
	$(RM) *.o *.dylib Heapz.class

protoc:
	$(PROTOC) --cpp_out=. profile.proto
