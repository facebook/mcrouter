FROM            ubuntu:16.04

LABEL           maintainer="mcrouter <mcrouter@fb.com>"

ENV             MCROUTER_DIR            /usr/local/mcrouter
ENV             MCROUTER_REPO           https://github.com/facebook/mcrouter.git
ENV             DEBIAN_FRONTEND         noninteractive

COPY		. $MCROUTER_DIR/repo

# sudo is not included in minimal Ubuntu install in 16.04+
RUN             apt-get update && apt-get install -y sudo && \
                cd $MCROUTER_DIR/repo/mcrouter/scripts && \
                ./install_ubuntu_16.04.sh $MCROUTER_DIR && \
                rm -rf $MCROUTER_DIR/repo && \
                ln -s $MCROUTER_DIR/install/bin/mcrouter /usr/local/bin/mcrouter

ENV             DEBIAN_FRONTEND newt
