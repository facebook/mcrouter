#!/bin/bash -e

apt -y update
apt -y install apt-transport-https \
                ca-certificates \
                tzdata \
                git
apt clean all