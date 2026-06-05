# WCL

Have you ever used `wc -l`? I have. If you are running said command on a processor modern enough to support vectorized instructions and multithreading (`wc` does those things too), `wcl` is faster.

Is it "blazingly fast"? Yes. On a dataset containing lots of small files, some medium files and a huge file:

```bash
❯ hyperfine --warmup 1 './wcl benchmarks/test-data/*' 'wc -l benchmarks/test-data/*'
Benchmark 1: ./wcl benchmarks/test-data/*
  Time (mean ± σ):      35.1 ms ±   2.8 ms    [User: 39.8 ms, System: 202.3 ms]
  Range (min … max):    31.4 ms …  40.9 ms    71 runs
 
Benchmark 2: wc -l benchmarks/test-data/*
  Time (mean ± σ):      1.732 s ±  0.004 s    [User: 1.603 s, System: 0.112 s]
  Range (min … max):    1.726 s …  1.736 s    10 runs
  
Summary
  ./wcl benchmarks/test-data/* ran
   49.37 ± 3.94 times faster than wc -l benchmarks/test-data/*
```


