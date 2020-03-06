#!/bin/bash

# run from base tgen directory

set -euo pipefail

# you must have already built tgen in the build dir
if [ -d "build" ] 
then
    echo "Running tests and storing test output in build/." 
else
    echo "Error: build/ directory does not exist, unable to run tests."
    exit 1
fi

# path to the test binary
build=./build
testbin=./${build}/test/test-mmodel

for seed in 123 321
do
	${testbin} ${seed} test/normal.mmodel.graphml | grep "with packet delay" | cut -d' ' -f15 | cut -d',' -f1 > ${build}/normal-${seed}
	${testbin} ${seed} test/lognormal.mmodel.graphml | grep "with packet delay" | cut -d' ' -f15 | cut -d',' -f1 > ${build}/lognormal-${seed}
	${testbin} ${seed} test/exponential.mmodel.graphml | grep "with packet delay" | cut -d' ' -f15 | cut -d',' -f1 > ${build}/exponential-${seed}
	${testbin} ${seed} test/pareto.mmodel.graphml | grep "with packet delay" | cut -d' ' -f15 | cut -d',' -f1 > ${build}/pareto-${seed}
done

fail=0

# run with multiple seeds and check determinism
for seed in 123 321
do
	for dist in normal-${seed} lognormal-${seed} exponential-${seed} pareto-${seed}
	do
		cmp ${build}/${dist} test/expected-results/${dist}
		error=$?
		
		if [ $error -eq 0 ]
		then
		   echo "$dist matches expected results and passes determinism test"
		elif [ $error -eq 1 ]
		then
		   echo "$dist does not match expected results and fails determinism test"
		   fail=1
		else
		   echo "There was something wrong with the cmp command"
		   fail=1
		fi
	done
done

if [ $fail -eq 0 ] 
then
    echo "All determinism tests passed!" 
else
    echo "At least one determinism test failed."
fi
exit ${fail}
