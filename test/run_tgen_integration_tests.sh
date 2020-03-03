

./build/src/tgen resource/server.tgenrc.graphml > build/tgen.server.log &
server_pid=$!

./build/src/tgen resource/client-singlefile.tgenrc.graphml > build/tgen.client-singlefile.log
./build/src/tgen resource/client-web.tgenrc.graphml > build/tgen.client-web.log

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
