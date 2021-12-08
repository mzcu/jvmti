JAVA=/Library/Java/JavaVirtualMachines/openjdk.jdk/Contents/Home
CXXFLAGS=-std=c++11 -I$(JAVA)/include -I$(JAVA)/include/darwin
LDFLAGS=-shared
TARGET=profi.dylib


$(TARGET): profi.o
	$(CXX) $(LDFLAGS) -o $@ $^ -lc

test:
	$(JAVA)/bin/java -Xmx16m -agentpath:./profi.dylib Main

buildMain:
	$(JAVA)/bin/javac Main.java

clean:
	$(RM) profi.dylib
	$(RM) profi.o

.PHONY: clean test buildMain
