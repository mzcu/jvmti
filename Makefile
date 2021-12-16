JAVA=/Library/Java/JavaVirtualMachines/openjdk.jdk/Contents/Home
CXXFLAGS=-std=c++20 -I$(JAVA)/include -I$(JAVA)/include/darwin -I/opt/homebrew/include
LDFLAGS=-shared -L/opt/homebrew/lib -lprotobuf
TARGET=libheapz.dylib
PROTOC=protoc


$(TARGET): profile.pb.o heapz.o
	$(CXX) $(LDFLAGS) -o $@ $^ -lc

test:
	$(JAVA)/bin/java -Xmx16m -agentpath:./$(TARGET) -Xint -XX:-Inline Main

buildMain:
	$(JAVA)/bin/javac Main.java

clean:
	$(RM) *.o *.dylib

protoc:
	$(PROTOC) --cpp_out=. profile.proto

.PHONY: clean test buildMain protoc
