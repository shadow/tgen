#!/bin/bash

# run from base tgen directory

rm -rf build-test
mkdir build-test
cd build-test
cmake ../test
make

./test-mmodel 123 ../test/normal.mmodel.graphml | grep "with packet delay" | cut -d' ' -f15 | cut -d',' -f1 > normal
./test-mmodel 123 ../test/lognormal.mmodel.graphml | grep "with packet delay" | cut -d' ' -f15 | cut -d',' -f1 > lognormal
./test-mmodel 123 ../test/exponential.mmodel.graphml | grep "with packet delay" | cut -d' ' -f15 | cut -d',' -f1 > exponential
./test-mmodel 123 ../test/pareto.mmodel.graphml | grep "with packet delay" | cut -d' ' -f15 | cut -d',' -f1 > pareto

python ../tools/scripts/plot-dist.py

echo "See the PDF files in the build-test directory"
