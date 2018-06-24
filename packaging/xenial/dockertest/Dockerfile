from ubuntu:xenial
MAINTAINER Ori Shalev (orish@fb.com)
RUN apt-get update && apt-get install -y libdouble-conversion1v5 openssl libgflags2v5 libgoogle-glog0v5 libboost-context1.58.0 libboost-filesystem1.58.0 libboost-program-options1.58.0 libboost-regex1.58.0 libevent-2.0-5
RUN apt-get install -y netcat
RUN mkdir /packages
VOLUME /packages
COPY config.json /root/config.json

#CMD bash
