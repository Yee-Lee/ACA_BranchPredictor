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

output files are suffixed with **.out** extension, like *vvadd.riscv.out*

# Profiling

        |NoBP      | Tsp+always true | BTB       |  Gshare only | Tsp+Gshare|
--------|--------- | --------------- | --------  |--------------|---------- |
dhr     |0.623651  | 0.195532        | 0.557129  | 0.246454     |   0.250685| 
med     |0.497022  | 0.373333        | 0.382523  | 0.342883     |   0.337297|
mul     |0.869362  | 0.118806        | 0.179226  | 0.142305     |   0.157511|
qso     |0.641522  | 0.263535        | 0.227269  | 0.207664     |   0.218088|
tow     |0.691003  | 0.368621        | 0.488781  | 0.373577     |   0.389790|
vva     |0.689732  | 0.193828        | 0.294693  | 0.210309     |   0.348758|
qsoM	|0.664607  |	-	     | 0.204865	 | 0.194685     |   0.200779|
spm	|0.685652  | 	-	     | 0.488004	 | 0.230794     | ` 0.246817|   

