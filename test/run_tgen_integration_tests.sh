#!/bin/bash

# run from base tgen directory

set -euo pipefail

# Allow TGEN to optionally be specified by caller. If it isn't,
# then assume it's in ./build/src/tgen.
TGEN=./build/src/tgen
while [[ ${1:-} ]]; do
  case "$1" in
    --tgen ) TGEN="$2"; shift 2 ;;
    * ) echo Unrecognized arg $1; exit 1; break ;;
  esac
done

$TGEN resource/server.tgenrc.graphml > build/tgen.server.log &
server_pid=$!

$TGEN resource/client-singlefile.tgenrc.graphml | tee build/tgen.client-singlefile.log
$TGEN resource/client-web.tgenrc.graphml | tee build/tgen.client-web.log

kill -9 ${server_pid}

grep "stream-success" build/tgen.client-singlefile.log | wc -l > build/client-singlefile-result
grep "stream-success" build/tgen.client-web.log | wc -l > build/client-web-result

fail=0

for result in client-singlefile-result client-web-result
do
	cmp build/${result} test/expected-results/${result}
	error=$?
	
	if [ $error -eq 0 ]
	then
	   echo "$result matches expected results and passes integration test"
	elif [ $error -eq 1 ]
	then
	   echo "$result does not match expected results and fails integration test"
	   fail=1
	else
	   echo "There was something wrong with the cmp command"
	   fail=1
	fi
done

if [ $fail -eq 0 ] 
then
    echo "All integration tests passed!" 
else
    echo "At least one integration test failed."
fi
exit ${fail}
