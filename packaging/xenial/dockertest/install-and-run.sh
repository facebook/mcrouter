#!/bin/bash

if [ $# -lt 1 ]; then
	echo "Usage: $0 <DEB-PACKAGE>"
	exit 2
fi

_user_interrupt() {
	echo "$0 killing child $child"
	kill "$child"
	echo "Killed."
}

debfile=$(basename "$1")
packagedir=$(dirname "$1")

docker build -t ready-to-install .
cid=$(docker run -d -it -v "$packagedir":/packages ready-to-install)
trap _user_interrupt INT
docker exec -i "$cid" bash -l -c "dpkg -i /packages/$debfile && mcrouter -p 11101 --config file:/root/config.json" &
child=$!
sleep 2
#docker exec -i "$cid" bash -l -c "echo stats | nc localhost 11101"
#sleep 1
echo "To enter the container, run in another terminal: \"docker exec -it $cid bash -l\""
echo "Press Ctrl-C to stop mcrouter, stop the container, and remove it."
wait $child
trap - INT
docker stop "$cid"
docker rm "$cid"
