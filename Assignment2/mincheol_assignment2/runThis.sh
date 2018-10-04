#!/bin/sh

THREAD=0

rm -rf results
mkdir results

for ITER in `seq 1 400`
do
	echo "ITER $ITER"
	
	for THREAD in 1 2 4
	do
		echo "THREAD $THREAD"
		./test_threads_coarse $THREAD >> results/coarse_$THREAD
		./test_threads_fine $THREAD >> results/fine_$THREAD
	done
done
