# Makefile for wsh

# Compiler and flags
CC := gcc
CFLAGS := -Wall -Werror -pedantic -std=gnu18

# Variables
LOGIN := yancheng
SUBMITPATH := ~cs537-1/handin/yancheng/P3

# Targets
.PHONY: all wsh run pack submit test

all: wsh

wsh: wsh.c 
	$(CC) $(CFLAGS) $^ -o $@

run: wsh
	./wsh

pack:
	tar -czf $(LOGIN).tar.gz wsh.h wsh.c Makefile README.md

submit: pack
	cp $(LOGIN).tar.gz $(SUBMITPATH)

test: 
	~cs537-1/tests/P3/test-cd.csh -c
	~cs537-1/tests/P3/test-exec.csh -c	
	~cs537-1/tests/P3/test-pipe.csh -c
	~cs537-1/tests/P3/test-job-control.csh -c

test_cd:
	~cs537-1/tests/P3/test-cd.csh -c
test_exec: 
	~cs537-1/tests/P3/test-exec.csh -c
test_jobs: 
	~cs537-1/tests/P3/test-job-control.csh -c
test_pipe:
	~cs537-1/tests/P3/test-pipe.csh -c

# Clean target to remove generated files
clean:
	rm -f wsh $(LOGIN).tar.gz
