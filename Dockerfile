FROM ubuntu:rolling

ARG DEBIAN_FRONTEND=noninteractive

RUN apt update && \
apt install -y \
build-essential \
xxd \
openjdk-17-jdk-headless

RUN apt install -y \
google-perftools \
vim

RUN mkdir -p /opt/heapz && cd /opt/heapz
ADD *.cc /opt/heapz
ADD *.h /opt/heapz
ADD Makefile /opt/heapz
ADD *.java /opt/heapz

RUN cd /opt/heapz \
&& echo "export JAVA_HOME=$(readlink -nf /usr/bin/java | xargs dirname | xargs dirname)" >> build.sh \
&& echo "cd /opt/heapz" >> build.sh \
&& echo "make clean all" >> build.sh \
&& echo "mv libheapz.so drop" >> build.sh

ENTRYPOINT exec bash /opt/heapz/build.sh
