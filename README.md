## omp-selfishloop

omp-selfishloop is a tool to measure OS activities using a busy
rdtsc-sampling loop on a multicore machine.

## Build and run example:

```
$ make
$ ./omp-selfishloop
# ndetours:   3000
# threshold:  1000
# timeout:    1000000000
# n. threads: 32
detour_mean=0.900440 %
```

As its name implies, this tool spawns threads using the OpenMP
parallel loop, so it accepts standard OpenMP environment variables.
For example, if you want to change the number of threads, set the
OMP_NUM_THREADS environment variable.

```
$ OMP_NUM_THREADS=2 ././omp-selfishloop
```

