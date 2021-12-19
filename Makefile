JAVA=/Library/Java/JavaVirtualMachines/openjdk.jdk/Contents/Home
CXXFLAGS=-std=c++20 -I$(JAVA)/include -I$(JAVA)/include/darwin -I/opt/homebrew/include -DDEBUG
LDFLAGS=-shared -L/opt/homebrew/lib -lprotobuf
TARGET=libheapz.dylib
PROTOC=protoc


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
