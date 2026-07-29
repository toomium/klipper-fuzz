#!/bin/bash

# afl settings to apply structure-aware fuzzing
export AFL_CUSTOM_MUTATOR_LIBRARY=./test/fuzzing/lpm_mutator/mutator.so
export AFL_CUSTOM_MUTATOR_ONLY=1
export AFL_DISABLE_TRIM=1
export AFL_POST_PROCESS_KEEP_ORIGINAL=1    # To signal AFL++ to save the protobuf formatted input instead of the post-process klipper format into to corpora

# other afl settings
export AFL_SKIP_CPUFREQ=1
export AFL_AUTORESUME=1
#export AFL_DEBUG=0

# mutator settings
#export SET_TARGET=MULTIMESSAGE
export SET_TARGET=SOS_FILTER_SET_SECTION
#export DEBUG=0

afl-fuzz -i fuzz/in/ -o fuzz/out4 ./out/klipper.elf
