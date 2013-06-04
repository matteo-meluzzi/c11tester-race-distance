CDSChecker Readme
=================

Copyright &copy; 2013 Regents of the University of California. All rights reserved.

CDSChecker is distributed under the GPL v2. See the LICENSE file for details.

This README is divided into sections as follows:

 *  Overview
 *  Basic build and run
 *  Running your own code
 *  Reading an execution trace
 *  References

Overview
--------

CDSChecker is a model checker for C11/C++11 exhaustively explores the behaviors
of code under the C11/C++11 memory model. It uses partial order reduction to
eliminate redundant executions to significantly shrink the state space.
The model checking algorithm is described in more detail in this paper
(currently under review):

  [http://demsky.eecs.uci.edu/publications/c11modelcheck.pdf](http://demsky.eecs.uci.edu/publications/c11modelcheck.pdf)

It is designed to support unit tests on concurrent data structure written using
C11/C++11 atomics.

CDSChecker is constructed as a dynamically-linked shared library which
implements the C and C++ atomic types and portions of the other thread-support
libraries of C/C++ (e.g., std::atomic, std::mutex, etc.). Notably, we only
support the C version of threads (i.e., `thrd_t` and similar, from `<threads.h>`),
because C++ threads require features which are only available to a C++11
compiler (and we want to support others, at least for now).

CDSChecker should compile on Linux and Mac OSX with no dependencies and has been
tested with LLVM (clang/clang++) and GCC. It likely can be ported to other \*NIX
flavors. We have not attempted to port to Windows.

Other references can be found at the main project page:

  [http://demsky.eecs.uci.edu/c11modelchecker.php](http://demsky.eecs.uci.edu/c11modelchecker.php)

Basic build and run
-------------------

Sample run instructions:

<pre>
$ make
$ export LD_LIBRARY_PATH=.
$ ./test/userprog.o                   # Runs simple test program
$ ./test/userprog.o -h                # Prints help information
Copyright (c) 2013 Regents of the University of California. All rights reserved.
Distributed under the GPLv2
Written by Brian Norris and Brian Demsky

Usage: ./test/userprog.o [MODEL-CHECKER OPTIONS] -- [PROGRAM ARGS]

MODEL-CHECKER OPTIONS can be any of the model-checker options listed below. Arguments
provided after the `--' (the PROGRAM ARGS) are passed to the user program.

Model-checker options:
-h, --help                  Display this help message and exit
-m, --liveness=NUM          Maximum times a thread can read from the same write
                              while other writes exist.
                              Default: 0
-M, --maxfv=NUM             Maximum number of future values that can be sent to
                              the same read.
                              Default: 0
-s, --maxfvdelay=NUM        Maximum actions that the model checker will wait for
                              a write from the future past the expected number
                              of actions.
                              Default: 6
-S, --fvslop=NUM            Future value expiration sloppiness.
                              Default: 4
-y, --yield                 Enable CHESS-like yield-based fairness support.
                              Default: disabled
-Y, --yieldblock            Prohibit an execution from running a yield.
                              Default: disabled
-f, --fairness=WINDOW       Specify a fairness window in which actions that are
                              enabled sufficiently many times should receive
                              priority for execution (not recommended).
                              Default: 0
-e, --enabled=COUNT         Enabled count.
                              Default: 1
-b, --bound=MAX             Upper length bound.
                              Default: 0
-v[NUM], --verbose[=NUM]    Print verbose execution information. NUM is optional:
                              0 is quiet; 1 is noisy; 2 is noisier.
                              Default: 0
-u, --uninitialized=VALUE   Return VALUE any load which may read from an
                              uninitialized atomic.
                              Default: 0
-t, --analysis=NAME         Use Analysis Plugin.
-o, --options=NAME          Option for previous analysis plugin.
                            -o help for a list of options
 --                         Program arguments follow.

Analysis plugins:
SC
</pre>


Note that we also provide a series of benchmarks (distributed separately),
which can be placed under the benchmarks/ directory. After building CDSChecker,
you can build and run the benchmarks as follows:

        cd benchmarks
        make
        ./run.sh barrier/barrier -y -m 2     # runs barrier test with fairness/memory liveness
        ./bench.sh                           # run all benchmarks twice, with timing results

Running your own code
---------------------

We provide several test and sample programs under the test/ directory, which
should compile and run with no trouble. Of course, you likely want to test your
own code. To do so, you need to perform a few steps.

First, because CDSChecker executes your program dozens (if not hundreds or
thousands) of times, you will have the most success if your code is written as a
unit test and not as a full-blown program.

Next, test programs should use the standard C11/C++11 library headers
(`<atomic>`/`<stdatomic.h>`, `<mutex>`, `<condition_variable>`, `<thread.h>`) and must
name their main routine as `user_main(int, char**)` rather than `main(int, char**)`.
We only support C11 thread syntax (`thrd_t`, etc. from `<thread.h>`).

Test programs may also use our included happens-before race detector by
including <librace.h> and utilizing the appropriate functions
(`store_{8,16,32,64}()` and `load_{8,16,32,64}()`) for loading/storing data from/to
non-atomic shared memory.

CDSChecker can also check boolean assertions in your test programs. Just
include `<model-assert.h>` and use the `MODEL_ASSERT()` macro in your test program.
CDSChecker will report a bug in any possible execution in which the argument to
`MODEL_ASSERT()` evaluates to false (that is, 0).

Test programs should be compiled against our shared library (libmodel.so) using
the headers in the `include/` directory. Then the shared library must be made
available to the dynamic linker, using the `LD_LIBRARY_PATH` environment
variable, for instance.

Reading an execution trace
--------------------------

When CDSChecker detects a bug in your program (or when run with the `--verbose`
flag), it prints the output of the program run (STDOUT) along with some summary
trace information for the execution in question. The trace is given as a
sequence of lines, where each line represents an operation in the execution
trace. These lines are ordered by the order in which they were run by CDSChecker
(i.e., the "execution order"), which does not necessarily align with the "order"
of the values observed (i.e., the modification order or the reads-from
relation).

The following list describes each of the columns in the execution trace output:

 * \#: The sequence number within the execution. That is, sequence number "9"
   means the operation was the 9th operation executed by CDSChecker. Note that
   this represents the execution order, not necessarily any other order (e.g.,
   modification order or reads-from).

 * t: The thread number

 * Action type: The type of operation performed

 * MO: The memory-order for this operation (i.e., `memory_order_XXX`, where `XXX` is
   `relaxed`, `release`, `acquire`, `rel_acq`, or `seq_cst`)

 * Location: The memory location on which this operation is operating. This is
   well-defined for atomic write/read/RMW, but other operations are subject to
   CDSChecker implementation details.

 * Value: For reads/writes/RMW, the value returned by the operation. Note that
   for RMW, this is the value that is *read*, not the value that was *written*.
   For other operations, 'value' may have some CDSChecker-internal meaning, or
   it may simply be a don't-care (such as `0xdeadbeef`).

 * Rf: For reads, the sequence number of the operation from which it reads.
   [Note: If the execution is a partial, infeasible trace (labeled INFEASIBLE),
   as printed during `--verbose` execution, reads may not be resolved and so may
   have Rf=? or Rf=Px, where x is a promised future value.]

 * CV: The clock vector, encapsulating the happens-before relation (see our
   paper, or the C/C++ memory model itself). We use a Lamport-style clock vector
   similar to [1]. The "clock" is just the sequence number (#). The clock vector
   can be read as follows:

   Each entry is indexed as CV[i], where

            i = 0, 1, 2, ..., <number of threads>

   So for any thread i, we say CV[i] is the sequence number of the most recent
   operation in thread i such that operation i happens-before this operation.
   Notably, thread 0 is reserved as a dummy thread for certain CDSChecker
   operations.

See the following example trace:

<pre>
------------------------------------------------------------------------------------
#    t    Action type     MO       Location         Value               Rf  CV
------------------------------------------------------------------------------------
1    1    thread start    seq_cst  0x7f68ff11e7c0   0xdeadbeef              ( 0,  1)
2    1    init atomic     relaxed        0x601068   0                       ( 0,  2)
3    1    init atomic     relaxed        0x60106c   0                       ( 0,  3)
4    1    thread create   seq_cst  0x7f68fe51c710   0x7f68fe51c6e0          ( 0,  4)
5    2    thread start    seq_cst  0x7f68ff11ebc0   0xdeadbeef              ( 0,  4,  5)
6    2    atomic read     relaxed        0x60106c   0                   3   ( 0,  4,  6)
7    1    thread create   seq_cst  0x7f68fe51c720   0x7f68fe51c6e0          ( 0,  7)
8    3    thread start    seq_cst  0x7f68ff11efc0   0xdeadbeef              ( 0,  7,  0,  8)
9    2    atomic write    relaxed        0x601068   0                       ( 0,  4,  9)
10   3    atomic read     relaxed        0x601068   0                   2   ( 0,  7,  0, 10)
11   2    thread finish   seq_cst  0x7f68ff11ebc0   0xdeadbeef              ( 0,  4, 11)
12   3    atomic write    relaxed        0x60106c   0x2a                    ( 0,  7,  0, 12)
13   1    thread join     seq_cst  0x7f68ff11ebc0   0x2                     ( 0, 13, 11)
14   3    thread finish   seq_cst  0x7f68ff11efc0   0xdeadbeef              ( 0,  7,  0, 14)
15   1    thread join     seq_cst  0x7f68ff11efc0   0x3                     ( 0, 15, 11, 14)
16   1    thread finish   seq_cst  0x7f68ff11e7c0   0xdeadbeef              ( 0, 16, 11, 14)
HASH 4073708854
------------------------------------------------------------------------------------
</pre>

Now consider, for example, operation 10:

This is the 10th operation in the execution order. It is an atomic read-relaxed
operation performed by thread 3 at memory address `0x601068`. It reads the value
"0", which was written by the 2nd operation in the execution order. Its clock
vector consists of the following values:

        CV[0] = 0, CV[1] = 7, CV[2] = 0, CV[3] = 10


References
----------

[1] L. Lamport. Time, clocks, and the ordering of events in a distributed
    system. CACM, 21(7):558-565, July 1978.