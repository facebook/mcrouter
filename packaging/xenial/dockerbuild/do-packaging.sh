#!/bin/bash

if [ $# -lt 3 ]; then
	echo "Usage: $0 <SRC-TARGZ> <DEBIAN-DIR> <DST-DIR>"
	exit 2
fi

srcdir=$(dirname "$1")
srctargz=$(basename "$1")
origtargz=$(echo "$srctargz" | tr '-' '_' | sed -e 's/tar.gz/orig.tar.gz/')
fileprefix=${origtargz/[.]orig.tar.gz/}
debiandir=$2
dstdir=$3

mcrouterdir=${srctargz/[.]tar.gz/}

if [ ! -f folly-2018.06.04.00.tar.gz ]; then
    wget -O folly-2018.06.04.00.tar.gz https://github.com/facebook/folly/archive/v2018.06.04.00.tar.gz
fi

docker build -t mcrouter-xenial-packaging .
cid=$(docker run -i -d -v "$srcdir":/src -v "$debiandir":/debdir -v "$dstdir":/dst mcrouter-xenial-packaging)
docker exec -it "$cid" bash -l -c "cp /src/$srctargz $origtargz && tar zxf $origtargz && cd $mcrouterdir && mkdir debian && cp -R /debdir/* debian/ && debuild -us -uc && cp ../$fileprefix* /dst/"
docker stop "$cid"
docker rm "$cid"
