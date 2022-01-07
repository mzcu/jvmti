FROM centos:8

RUN yum install -y dnf-plugins-core && yum config-manager --set-enabled powertools && yum update -y
RUN yum -y groupinstall 'Development Tools' && \
yum install -y vim-common java-11-openjdk-devel libstdc++-static


RUN mkdir -p /opt/heapz && cd /opt/heapz
ADD *.cc /opt/heapz
ADD *.h /opt/heapz
ADD Makefile /opt/heapz
ADD *.java /opt/heapz

RUN cd /opt/heapz \
&& echo "export JAVA_HOME=$(readlink -nf /usr/bin/javac | xargs dirname | xargs dirname)" >> build.sh \
&& echo "cd /opt/heapz" >> build.sh \
&& echo "make clean all" >> build.sh \
&& echo "cp libheapz.so drop" >> build.sh

ENTRYPOINT exec bash /opt/heapz/build.sh
