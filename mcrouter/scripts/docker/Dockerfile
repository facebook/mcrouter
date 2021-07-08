FROM            ubuntu:20.04

MAINTAINER      mcrouter <mcrouter@fb.com>

ENV             MCROUTER_DIR            /usr/local/mcrouter
ENV             MCROUTER_REPO           https://github.com/facebook/mcrouter.git
ENV             DEBIAN_FRONTEND         noninteractive

RUN             apt-get update && apt-get --no-install-recommends install -y git && \
                apt-get clean && \
                apt-get purge autoremove && \
                rm -rf \
                      /var/lib/apt/lists/* \
                      /tmp/* \
                      /var/tmp/* \
                      /usr/share/man \
                      /usr/share/doc \
                      /usr/share/doc-base && \
                mkdir -p $MCROUTER_DIR/repo && \
                cd $MCROUTER_DIR/repo && git clone $MCROUTER_REPO && \
                cd $MCROUTER_DIR/repo/mcrouter/mcrouter/scripts && \
                ./install_ubuntu_20.04.sh $MCROUTER_DIR && \
                ./clean_ubuntu_14.04.sh $MCROUTER_DIR && rm -rf $MCROUTER_DIR/repo && \
                ln -s $MCROUTER_DIR/install/bin/mcrouter /usr/local/bin/mcrouter

ENV             DEBIAN_FRONTEND newt
