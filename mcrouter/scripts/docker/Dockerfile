ARG             UBUNTU_VERSION=14.04
FROM            ubuntu:${UBUNTU_VERSION}

MAINTAINER      mcrouter <mcrouter@fb.com>

ENV             MCROUTER_DIR            /usr/local/mcrouter
ENV             MCROUTER_REPO           https://github.com/facebook/mcrouter.git
ENV             DEBIAN_FRONTEND         noninteractive

ARG             UBUNTU_VERSION
ARG             RELEASE_REV=master
RUN             apt-get update && apt-get install -y software-properties-common git sudo && \
                add-apt-repository ppa:george-edison55/cmake-3.x && \
                mkdir -p $MCROUTER_DIR/repo && \
                cd $MCROUTER_DIR/repo && git clone $MCROUTER_REPO && \
                cd $MCROUTER_DIR/repo/mcrouter && git checkout ${RELEASE_REV} && \
                cd $MCROUTER_DIR/repo/mcrouter/mcrouter/scripts && \
                ./install_ubuntu_${UBUNTU_VERSION}.sh $MCROUTER_DIR && \
                { ./clean_ubuntu_${UBUNTU_VERSION}.sh $MCROUTER_DIR || true ; } && rm -rf $MCROUTER_DIR/repo && \
                ln -s $MCROUTER_DIR/install/bin/mcrouter /usr/local/bin/mcrouter

ENV             DEBIAN_FRONTEND newt
