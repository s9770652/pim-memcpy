# Transferring Data Between Different Memories

## The Official Way
The UPMEM documentation focuses on two functions for accesses to the MRAM: `mram_read(from, to, size)` and `mram_write(from, to, size)`.
They allow to load blocks of data from the MRAM into a designated cache in the WRAM and vice versa.
Their usage is encumbered by multiple rules:

* The transfer size must be at least 8 bytes and at most 2048 bytes.
  The transfer size must be a multiple of 8.

* The MRAM address must be aligned on 8 bytes.

* The WRAM address must be aligned on 8 bytes.

Failing to follow these can lead to compilation errors but also to data being written to wrong addresses, data not being transferred or data being replaced by seemingly random nonsense.



## Using a String Function

However, there is one—barely documented—function called `memcpy(to, from, size)`, provided by `string.h`, which also serves to copy data.
It offers several advantages:

* The transfer size can be arbitrary.
* No address must be aligned on 8 bytes.
* Transfers from one MRAM address to another MRAM address is possible without any intermediate cache in WRAM.

Let us make a quick comparison:
Suppose one has an MRAM array `output` filled with the numbers from 100 to 116:
```
100 101 102 103 104 105 106 107 108 109 110 111 112 113 114 115 116
```
Also, there is a WRAM array `cache` filled with the numbers from 0 to 16.
Calling `memcpy(&output[1], cache, 16 * sizeof(int32_t))` works as expected;
only the first digit, the one hundred, remains in `output` and everything else is set to the smaller values from the cache:
```
100   0   1   2   3   4   5   6   7   8   9  10  11  12  13  14  15
```
Transferring not a multiple of 8 bytes also works as calling `memcpy(&output[1], cache, 15 * sizeof(int32_t))` shows:
```
100   0   1   2   3   4   5   6   7   8   9  10  11  12  13  14 116
```
Restricting oneself to the intended functions yields worse results.
The call `mram_write(cache, &output[1], 16 * sizeof(int32_t))` gives the following:
```
  0   1   2   3   4   5   6   7   8   9  10  11  12  13  14  15 116
```
Instead of starting at index 1, the new numbers start at index 0!
This is because the numbers are 4 bytes long, meaning that the address `&output[1]` is not aligned on 8 bytes.
The very same result is given by the code `mram_write(&cache[1], output, 16 * sizeof(int32_t))` even though the new numbers starting at `1` would be expected.
The call `mram_write(cache, &output[1], 15 * sizeof(int32_t))` does not even compile as the transfer size is not a multiple of 8.
On top of that, it is now necessary to pollute the code with the `__dma_aligned` keyword to even attempt to handle the alignment of WRAM variables.



## Comparing Performance

At first glance, `memcpy` seems to be the better choice.
However, it very much depends on the amount of data transferred.
In this little test suite, 31 MiB of data are transferred from one MRAM array to another.
The baseline is a simple for loop with element-wise, direct accesses (`output[i] = input[i]`).
It takes a total of 2184 ms.
When copying all data using `memcpy` without using any cache in WRAM, the time shrinks by 75% down to 546 ms.

When employing `mram_read` and `mram_write`, a transfer size of 8 B yields a runtime of 2269 ms, so an even worse time than the baseline.
Increasing the transfer size significantly improves the runtime, with 64 B leading to 373 ms, which is 83% less than the baseline and 32% less than `memcpy`.
At the maximum transfer size of 2048 B, a runtime of just 109 ms is achieved, 95% less than the baseline and 80% less than `memcpy`.

The performance of `memcpy` cannot be improved by mimicking the behaviour of `mram_read` and `mram_write` by first writing to a WRAM cache and then to the destination in MRAM.
Quite to the contrary, the measured times reach new heights in seemingly erratic order—which at least stay consistent between runs.
Due to the poor documentation of this function, no speculations will be given here.

The following table contains all runtimes in mili seconds;
the transfer sizes are given in bytes.

| Transfer Size | memcpy | mram_read/write |
| ------------- | -----: | --------------: |
|          none |    546 |               / |
|             8 |   2802 |            2269 |
|            16 |   2490 |            1192 |
|            32 |   2269 |             662 |
|            64 |   5215 |             373 |
|           128 |   4467 |             235 |
|           256 |   4079 |             165 |
|           512 |   3894 |             131 |
|          1024 |   3800 |             114 |
|          2048 |   3752 |             109 |




## Conclusion and Final Remarks

The function `memcpy` trumps `mram_read` and `mram_write` when it comes to usability as no loops, bounds checking and alignment precautions are needed.
When it comes to speed, `memcpy` still holds the upper hand if the transfer sizes are not too big.
It should be noted that `mram_read` and `mram_write` were tested without much overhead.
In some applications, sophisticated logic for checking bounds and guaranteeing alignment may raise the threshold even further.

The little attention `memcpy` receives in the documentation may be off-putting to some.
In fact, all of the advantages listed in ‘Using a String Function’ are purely observations and I give no guarantee for their universality.