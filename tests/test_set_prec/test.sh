#!/bin/bash
#set -e

export VFC_PREC_INPUT="config.txt"
verificarlo test.c -o test -lm --inst-func 2> /dev/null

rm -f output.txt

for i in `seq 1 52`; do 
	echo "test.c/powf_113	1	0	1	0	$i	11	23	8	1
test.c/float_fct_107	0	0	1	0	$i	11	23	8	1
test.c/double_fct_92	0	0	0	1	$i	11	23	8	1
test.c/float_fct_73	0	0	1	0	$i	11	23	8	1
test.c/pow_98	1	0	0	1	$i	11	23	8	1
test.c/double_fct_50	0	0	0	1	$i	11	23	8	1" > config.txt
	export VFC_BACKENDS="libinterflop_vprec.so --mode=ib"
	./test $i 23 >> output.txt 2> /dev/null
	export VFC_BACKENDS="libinterflop_vprec.so --mode=ob"
	./test $i 23 >> output.txt 2> /dev/null
	echo "--------------------------------------------------------" >> output.txt
done 

for i in `seq 1 23`; do
	echo "test.c/powf_113	1	0	1	0	52	11	$i	8	1
test.c/float_fct_107	0	0	1	0	52	11	$i	8	1
test.c/double_fct_92	0	0	0	1	52	11	$i	8	1
test.c/float_fct_73	0	0	1	0	52	11	$i	8	1
test.c/pow_98	1	0	0	1	52	11	$i	8	1
test.c/double_fct_50	0	0	0	1	52	11	$i	8	1" > config.txt
	export VFC_BACKENDS="libinterflop_vprec.so --mode=ib"
	./test 52 $i >> output.txt 2> /dev/null
	export VFC_BACKENDS="libinterflop_vprec.so --mode=ob"
	./test 52 $i >> output.txt 2> /dev/null
	echo "--------------------------------------------------------" >> output.txt
done

echo $(diff -U 0 result.txt output.txt | grep ^@ | wc -l)

if [ $(diff -U 0 result.txt output.txt | grep ^@ | wc -l) == 0 ]
then 
	exit 1
else 
	exit 0
fi