##======- lib/*/*/Makefile ------------------------------*- Makefile -*-======##
##===----------------------------------------------------------------------===##

LEVEL ?= ../../..

all:
	make -C SAGE LEVEL=../$(LEVEL)
	./SAGE/bin/sage -sh -c "make -f Makefile.llvm LEVEL=$(LEVEL)"

