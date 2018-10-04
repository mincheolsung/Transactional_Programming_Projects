#!/bin/sh

THREAD=0

rm -rf results
mkdir results

for ITER in `seq 1 100`
do
	echo "ITER $ITER"
	
	for THREAD in 1 2 4
	do
		echo "THREAD $THREAD"
		./stm_1000000 $THREAD >> results/1000000_$THREAD
		./stm_1000 $THREAD >> results/1000_$THREAD
		./stm_1000000_disjoint $THREAD >> results/1000000_disjoint_$THREAD
		./stm_1000_disjoint $THREAD >> results/1000_disjoint_$THREAD
	done
done
