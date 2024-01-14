#! /bin/csh -f
set TEST_HOME = /p/course/cs537-oliphant/tests/P1 
set source_file = wman.c
set binary_file = wman
set bin_dir = ${TEST_HOME}/bin
set test_dir = ${TEST_HOME}/tests-wman

${bin_dir}/generic-tester.py -s $source_file -b $binary_file -t $test_dir $argv[*]
