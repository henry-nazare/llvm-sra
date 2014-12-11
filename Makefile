##======- lib/*/*/Makefile ------------------------------*- Makefile -*-======##
##===----------------------------------------------------------------------===##

LEVEL ?= ../../..

all:
	make -C SAGE LEVEL=$(LEVEL)/..
	./SAGE/sage/sage -sh -c "make -f Makefile.llvm LEVEL=$(LEVEL)"

