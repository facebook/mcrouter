from ubuntu:xenial
MAINTAINER Ori Shalev (orish@fb.com)
RUN apt-get update && apt-get install -y debhelper cmake g++ libdouble-conversion-dev libgoogle-glog-dev libevent-dev libssl-dev libboost-all-dev libgtest-dev devscripts autoconf git liblzma-dev libdwarf-dev libaio-dev liburcu-dev libbz2-dev libiberty-dev google-mock ragel libunwind-dev libsodium-dev

ARG follycommit=d98241e246d3af96bcb14b03bae4101ccbf9f533
ARG fizzcommit=master
ARG wanglecommit=c1bc5a034d949309215a9616af0b41c15c347792

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

VOLUME /src
VOLUME /dst
VOLUME /debdir
RUN mkdir /root/work
WORKDIR /root/work
