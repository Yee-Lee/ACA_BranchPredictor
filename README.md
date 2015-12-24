#ACA_BranchPredictor


Can someone provide the information about running the testcase?
Including:

1. how to compile the code correctly
2. how to run the emulation
3. explanation of the output
4. how to quantify the output

Compile
======================

    $ cd $RISCV/riscv-sodor/emulator/rv32_5stage_bp_aca
    $ make emulator

clean old builds and output files

    $ make clean

Run emulator
=====================

run all asembly tests and benchmarks

    $ make run

run only asembly tests

    $ make run-asm-tests

run only benchmarks

    $ make run-bmarks-test


Output checking
=====================

All output files are stored at **output** folder
    
    $ cd output

output files are suffixed with **.out** extension, like *vvadd.riscv.out*

    

Profiling
=====================
