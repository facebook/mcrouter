#!/bin/bash

if [ $# -lt 2 ]; then
	echo "Usage: $0 <xenial|bionic> <DEB-PACKAGE>"
	exit 2
fi

_user_interrupt() {
	echo "$0 killing child $child"
	kill "$child"
	echo "Killed."
}

dist=$1
debfile=$(basename "$2")
packagedir=$(dirname "$2")

docker build --file=Dockerfile-$dist -t ready-to-install-$dist .
cid=$(docker run -d -it -v "$packagedir":/packages ready-to-install-$dist)
trap _user_interrupt INT
docker exec -i "$cid" bash -l -c "dpkg -i /packages/$debfile && mcrouter -p 11101 --config file:/root/config.json" &
child=$!
sleep 2
echo "To enter the container, run in another terminal: \"docker exec -it $cid bash -l\""
echo "Press Ctrl-C to stop mcrouter, stop the container, and remove it."
wait $child
trap - INT
docker stop "$cid"
docker rm "$cid"
