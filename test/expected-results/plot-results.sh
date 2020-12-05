# run from test/expected-results/ directory.
# note that the plot script requires matplotlib, numpy, and scipy
for seed in 123 321
do
	python3 ../../tools/scripts/plot-dist.py ${seed}
done

echo "See the PDF files in the build-test directory"
