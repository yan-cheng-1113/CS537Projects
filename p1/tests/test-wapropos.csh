#! /bin/csh -f
set TEST_HOME = /p/course/cs537-oliphant/tests/P1 
set source_file = wapropos.c
set binary_file = wapropos
set bin_dir = ${TEST_HOME}/bin
set test_dir = ${TEST_HOME}/tests-wapropos

${bin_dir}/generic-tester.py -s $source_file -b $binary_file -t $test_dir $argv[*]
