from ubuntu:xenial
MAINTAINER Ori Shalev (orish@fb.com)
RUN apt-get update && apt-get install -y debhelper cmake g++ libdouble-conversion-dev libgoogle-glog-dev libevent-dev libssl-dev libboost-all-dev libgtest-dev devscripts autoconf git liblzma-dev libdwarf-dev libaio-dev liburcu-dev libbz2-dev libiberty-dev google-mock ragel

ARG follycommit=137443e869b5c293bf43adec8dd31d9800c943a1
ARG wanglecommit=001d0e5f4c7548b721f5f7ea4e7d04d5efee9280

WORKDIR /root
RUN git clone https://github.com/facebook/folly.git && cd folly && git checkout $follycommit
WORKDIR /root/folly
RUN cmake .
RUN make install

# Ugly workaround due to missing stuff from source folly install:
# Need to install this one release once and then re-install the one from the
# commit hash. If we don't do that, the wangle cmake fails!
RUN mkdir /root/relfolly
COPY folly-2018.06.04.00.tar.gz /root/relfolly
WORKDIR /root/relfolly
RUN tar zxf *.gz
WORKDIR /root/relfolly/folly-2018.06.04.00
RUN cmake .
RUN make install
WORKDIR /root/folly
RUN make install

WORKDIR /root
RUN git clone https://github.com/facebook/wangle.git && cd wangle && git checkout $wanglecommit
WORKDIR /root/wangle
COPY wangle.patch /tmp/wangle.patch
RUN git apply /tmp/wangle.patch
WORKDIR /root/wangle/wangle
RUN cmake .
RUN make install

VOLUME /src
VOLUME /dst
VOLUME /debdir
RUN mkdir /root/work
WORKDIR /root/work
