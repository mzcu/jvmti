JAVA=/Library/Java/JavaVirtualMachines/openjdk.jdk/Contents/Home
CXXFLAGS=-std=c++20 -I$(JAVA)/include -I$(JAVA)/include/darwin -I/opt/homebrew/include
LDFLAGS=-shared -L/opt/homebrew/lib -lprotobuf
TARGET=profi.dylib
PROTOC=protoc


$(TARGET): profile.pb.o profi.o
	$(CXX) $(LDFLAGS) -o $@ $^ -lc

test:
	$(JAVA)/bin/java -Xmx16m -agentpath:./profi.dylib -Xint -XX:-Inline Main

buildMain:
	$(JAVA)/bin/javac Main.java

clean:
	$(RM) profi.dylib
	$(RM) profi.o

protoc:
	$(PROTOC) --cpp_out=. profile.proto

.PHONY: clean test buildMain protoc
