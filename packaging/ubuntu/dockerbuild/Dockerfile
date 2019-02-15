ARG dist
FROM ubuntu:${dist}
MAINTAINER Ori Shalev (orish@fb.com)
RUN apt-get update && apt-get install -y debhelper cmake g++ libdouble-conversion-dev libgoogle-glog-dev libevent-dev libssl-dev libboost-all-dev libgtest-dev devscripts autoconf git liblzma-dev libdwarf-dev libaio-dev liburcu-dev libbz2-dev libiberty-dev google-mock ragel libunwind-dev libsodium-dev bison flex

ARG follycommit
ARG fizzcommit
ARG wanglecommit
ARG thriftcommit
ARG zstdcommit=v1.3.7

WORKDIR /root
RUN git clone https://github.com/facebook/zstd.git && cd zstd && git checkout $zstdcommit
WORKDIR /root/zstd
RUN make install

WORKDIR /root
RUN git clone https://github.com/facebook/folly.git && cd folly && git checkout $follycommit
WORKDIR /root/folly
RUN cmake .
RUN make install

WORKDIR /root
RUN git clone https://github.com/facebookincubator/fizz.git && cd fizz && git checkout $fizzcommit
WORKDIR /root/fizz/fizz
RUN cmake .
RUN make install

WORKDIR /root
RUN git clone https://github.com/facebook/wangle.git && cd wangle && git checkout $wanglecommit
WORKDIR /root/wangle/wangle
RUN cmake .
RUN make install

WORKDIR /root
RUN git clone https://github.com/no1msd/mstch.git && cd mstch
WORKDIR /root/mstch
RUN cmake .
RUN make install

WORKDIR /root
RUN git clone https://github.com/facebook/fbthrift.git && cd fbthrift && git checkout $thriftcommit
WORKDIR /root/fbthrift
RUN cmake .
RUN make install

VOLUME /src
VOLUME /dst
VOLUME /debdir
RUN mkdir /root/work
WORKDIR /root/work

RUN sed -i /usr/local/include/wangle/ssl/SSLContextConfig.h -e 's/SSLServerOptions/SSLCommonOptions/'

ARG dist
