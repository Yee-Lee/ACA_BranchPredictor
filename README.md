# ACA BranchPredictor

Can someone provide the information about running the testcase?
Including:

1. how to compile the code correctly
2. how to run the emulation
3. explanation of the output
4. how to quantify the output

# Compile

    $ cd $RISCV/riscv-sodor/emulator/rv32_5stage_bp_aca
    $ make emulator

clean old builds and output files

    $ make clean

# Run emulator

run all asembly tests and benchmarks

    $ make run

run only asembly tests

    $ make run-asm-tests

run only benchmarks

    $ make run-bmarks-test

mannually launch emulator 

    $./emulator +max-cycles=10000000000  +verbose +coremap-random +loadmem={WHERE_YOUR_APP} none 2>/dev/null

# Output checking

All output files are stored at **output** folder

    $ cd output
    $ for f in `ls *.out`
    $ do
    $     echo $f && tail $f | grep MISS
    $ done

output files are suffixed with **.out** extension, like *vvadd.riscv.out*

# Profiling

        |NoBP      | Tsp+always true | BTB       | Gshare only  | Tsp+Gshare    | 
--------|--------- | --------------- | --------  |--------------|-------------- |
dhr     |0.623651  | 0.195532        | 0.557129  | 0.212528     |  0.215618     | 
med     |0.497022  | 0.373333        | 0.382523  | 0.285143     |  0.287505     | 
mul     |0.869362  | 0.118806        | 0.179226  | 0.139362     |  0.141019     | 
qso     |0.641522  | 0.263535        | 0.227269  | 0.181992     |  0.186374     | 
tow     |0.691003  | 0.368621        | 0.488781  | 0.405527     |  0.411054     | 
vva     |0.689732  | 0.193828        | 0.294693  | 0.209951     |  0.216359     | 
qsoM	|0.664607  | 0.261084        | 0.204865	 | 0.169804     |  0.174152     | 
spm	    |0.685652  | 0.330472        | 0.488004	 | 0.248205     |  0.259307     |       
