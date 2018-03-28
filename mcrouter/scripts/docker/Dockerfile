FROM            ubuntu:14.04

MAINTAINER      mcrouter <mcrouter@fb.com>

ENV             MCROUTER_DIR            /usr/local/mcrouter
ENV             MCROUTER_REPO           https://github.com/facebook/mcrouter.git
ENV             DEBIAN_FRONTEND         noninteractive
ARG             MAKE_JOBS=1

RUN             apt-get update && apt-get install -y git && \
                mkdir -p $MCROUTER_DIR/repo && \
                cd $MCROUTER_DIR/repo && git clone $MCROUTER_REPO && \
                cd $MCROUTER_DIR/repo/mcrouter/mcrouter/scripts && \
                ./install_ubuntu_14.04.sh $MCROUTER_DIR -j$MAKE_JOBS && \
                ./clean_ubuntu_14.04.sh $MCROUTER_DIR && rm -rf $MCROUTER_DIR/repo && \
                ln -s $MCROUTER_DIR/install/bin/mcrouter /usr/local/bin/mcrouter

ENV             DEBIAN_FRONTEND newt
