# Symbolic range analysis for LLVM

## Install instructions

Update the submodules recursively and call make, as seen below.

    git submodule update --init --recursive
    make

If the required version of SAGE is not installed, it will be downloaded and
decompressed into the *SAGE/* directory.

Makefile requires that the repository be cloned into one of the
subdirectories of *lib/* in the LLVM tree, such as *Transforms/*.

## Executing
We require the use of *SAGE/bin/sage-opt* as a replacement for LLVM's
own *opt* when executing any of the included passes.
Our range analysis requires that the CFG be in e-SSA form, achieved by
running the redefinition pass with *-redef*. Afterwards, we run the
range analysis itself on the resulting bytecode with *-sra*. The
Python, SAGE, and SRA libraries must be loaded before executing said
passes.

For Linux, the command line should be something like:

    SAGE/bin/sage-opt -load Python.so -load SAGE.so -load SRA.so -mem2reg -redef -sra <bytecode>

For Mac, use the following:

    SAGE/bin/sage-opt -load Python.dylib -load SAGE.dylib -load SRA.dylib -mem2reg -redef -sra <bytecode>

