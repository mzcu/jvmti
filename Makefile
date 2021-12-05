CXXFLAGS=-std=c++11 -I/Library/Java/JavaVirtualMachines/openjdk.jdk/Contents/Home/include -I/Library/Java/JavaVirtualMachines/openjdk.jdk/Contents/Home/include/darwin
LDFLAGS=-shared
TARGET=profi.dylib


$(TARGET): profi.o
	$(CXX) $(LDFLAGS) -o $@ $^ -lc

test:
	java -Xmx16m -agentpath:./profi.dylib Main

buildMain:
	javac Main.java

clean:
	$(RM) profi.dylib
	$(RM) profi.o

.PHONY: clean test buildMain
