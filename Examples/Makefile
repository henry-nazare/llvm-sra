##======- lib/*/*/Examples/Makefile ---------------------*- Makefile -*-======##
##===----------------------------------------------------------------------===##

SOURCES=$(wildcard *.c)
LLS=$(SOURCES:.c=.ll)
TESTLLS=$(LLS:.ll=.out.ll)
TESTOUT=$(LLS:.ll=.out)

all: $(LLS) $(TESTLLS) $(TESTOUT)

%.ll: %.c
	clang $(CFLAGS) -S -emit-llvm $< -o $@

%.out %.out.ll: %.ll
	../SAGE/bin/sage-opt -load Python.so -load SAGE.so -load SRA.so -mem2reg \
	                     -sra-annotator -sra-gen-test -S -o $*.out.ll $< 2>$*.out

.PHONY: clean

clean:
	rm $(LLS) $(TESTLLS) $(TESTOUT)

