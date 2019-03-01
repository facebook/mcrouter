#!/bin/bash

if [ $# -lt 4 ]; then
	echo "Usage: $0 <xenial|bionic> <SRC-TARGZ> <DEBIAN-DIR> <DST-DIR>"
	exit 2
fi

dist=$1
srcdir=$(dirname "$2")
srctargz=$(basename "$2")
origtargz=$(echo "$srctargz" | tr '-' '_' | sed -e 's/.tar.gz/.orig.tar.gz/')
fileprefix=${origtargz/[.]orig.tar.gz/}
debiandir=$3
dstdir=$4

mcrouterdir=${srctargz/[.]tar.gz/}
releasetag=$(echo $fileprefix | sed -r -e 's/.*_0[.]([0-9]+)[.]([0-9]+).*/release-\1-\2/')

follycommit="$(curl https://raw.githubusercontent.com/facebook/mcrouter/$releasetag/mcrouter/FOLLY_COMMIT 2>/dev/null)"
fizzcommit="$(curl https://raw.githubusercontent.com/facebook/mcrouter/$releasetag/mcrouter/FIZZ_COMMIT 2>/dev/null)"
wanglecommit="$(curl https://raw.githubusercontent.com/facebook/mcrouter/$releasetag/mcrouter/WANGLE_COMMIT 2>/dev/null)"
thriftcommit="$(curl https://raw.githubusercontent.com/facebook/mcrouter/$releasetag/mcrouter/FBTHRIFT_COMMIT 2>/dev/null)"

echo "folly commit: $follycommit"
echo "fizz commit: $fizzcommit"
echo "wangle commit: $wanglecommit"
echo "fbthrift commit: $thriftcommit"
buildargs="--build-arg follycommit=$follycommit --build-arg fizzcommit=$fizzcommit --build-arg wanglecommit=$wanglecommit --build-arg thriftcommit=$thriftcommit"

docker build --build-arg dist=$dist $buildargs -t mcrouter-${dist}-packaging .
docker run -i -d -v "$srcdir":/src -v "$debiandir":/debdir -v "$dstdir":/dst mcrouter-${dist}-packaging /bin/bash -l
cid=$(docker run -i -d -v "$srcdir":/src -v "$debiandir":/debdir -v "$dstdir":/dst mcrouter-${dist}-packaging)
docker exec -it "$cid" bash -l -c "cp /src/$srctargz $origtargz && tar zxf $origtargz && cd $mcrouterdir && mkdir debian && cp -R /debdir/* debian/ && sed -e \"s/DIST/$dist/\" -i debian/changelog && cp debian/control.$dist debian/control && debuild -us -uc && cp ../$fileprefix* /dst/"
docker stop "$cid"
docker rm "$cid"

