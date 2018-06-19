from ubuntu:xenial
MAINTAINER Ori Shalev (orish@fb.com)
RUN apt-get update && apt-get install -y debhelper cmake g++ libdouble-conversion-dev libgoogle-glog-dev libevent-dev libssl-dev libboost-all-dev libgtest-dev devscripts autoconf git liblzma-dev libdwarf-dev libaio-dev liburcu-dev libbz2-dev libiberty-dev google-mock ragel

ARG follycommit=ed88a3a34ea0514c25621ecd06e3e1af14b078e7
ARG wanglecommit=356e60fa9c5a7eaeb2a93e88c9a939b6530d4c7f

WORKDIR /root
RUN git clone https://github.com/facebook/folly.git && cd folly && git checkout $follycommit
WORKDIR /root/folly
RUN cmake .
RUN make install

WORKDIR /root
RUN git clone https://github.com/facebook/wangle.git && cd wangle && git checkout $wanglecommit
WORKDIR /root/wangle/wangle
RUN cmake .
RUN make install

VOLUME /src
VOLUME /dst
VOLUME /debdir
RUN mkdir /root/work
WORKDIR /root/work
