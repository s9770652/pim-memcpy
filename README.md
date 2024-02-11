# Transferring Data Between Different Memories

*The code can be run using `make run`.
Please note that some tests in `task.c` are commented out due to overflows when uncommenting everything.
Until the issue is fixed, users should compile only parts of the code.*

## The Official Way
The UPMEM documentation focuses on two functions for accesses to the MRAM: `mram_read(from, to, size)` and `mram_write(from, to, size)`.
They allow to load blocks of data from the MRAM into a designated cache in the WRAM and and the other way around.
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
* Transfers from one MRAM address to another MRAM address are possible without any intermediate cache in WRAM.

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


### Static Versus Dynamic Cache

For the table above, the cache was allocation statically via `int32_t cache[2048]`.
Curiously, changing to a dynamic allocation via `int32_t *cache = mem_alloc(2048 * sizeof(int32_t))` betters the times of `mram_read` and `mram_write` but worsens the time of `memcpy` with a cache.
The effect first weakens and then disappears with increased transfer size.

| Transfer Size | memcpy | mram_read/write |
| ------------- | -----: | --------------: |
|          none |    546 |               / |
|             8 |   3666 |            1923 |
|            16 |   5056 |             994 |
|            32 |   5795 |             555 |
|            64 |   5215 |             330 |
|           128 |   4467 |             212 |
|           256 |   4080 |             155 |
|           512 |   3893 |             125 |
|          1024 |   3800 |             113 |
|          2048 |   3753 |             109 |

#### An Attempt at an Explanation

In the following short program, 8126464 `int32_t` are moved from `input` to `output` in batches of 2 numbers.
With dynamically allocated cache, the runtime is about 1.95 s, whereas it is 2.2 s with static allocation.
```
#include <stddef.h>

#include <alloc.h>
#include <mram.h>

#define LOAD_INTO_MRAM ((1024 * 1024 * 31) / sizeof(int32_t))

int32_t __mram_noinit input[LOAD_INTO_MRAM];
int32_t __mram_noinit output[LOAD_INTO_MRAM];

int main() {
    // int32_t __dma_aligned cache[2];
    int32_t *cache = mem_alloc(8);

    for (size_t i = 0; i < LOAD_INTO_MRAM; i += 2) {
        mram_read(&input[i], cache, 8);
        mram_write(cache, &output[i], 8);
    }
}
```

In case of dynamic allocation, the compilation of the for loop is the following (`r0` holds the address of the cache, `r1` the index `i`, `r2` the address `&output[i]`, and `r3` the address `&input[i]`):
```
move r4, 8126462                   // LOAD_INTO_MRAM - 2
.LBB0_1:
	ldma r0, r3, 0                 // mram_read(&input[i], cache, 8)
	sdma r0, r2, 0                 // mram_write(cache, &output[i], 8)
	add r1, r1, 2                  // i += 2
	add r2, r2, 8                  // &output[i] → &output[i+2]
	add r3, r3, 8                  // &input[i] → &input[i+2]
	jltu r1, r4, .LBB0_1
```

In case of static allocation, the compilation of the for loop is the following (`r0` holds the index `i`, `r1` the address `&output[i]`, and `r2` the address `&input[i]`):
```
.LBB0_1:
	add r3, r22, 0                 // load address of the cache from the stack
	ldma r3, r2, 0                 // mram_read(&input[i], cache, 8)
	sdma r3, r1, 0                 // mram_write(cache, &output[i], 8)
	add r0, r0, 2                  // i += 2
	add r1, r1, 8                  // &output[i] → &output[i+2]
	move r3, 8126462               // LOAD_INTO_MRAM - 2
	add r2, r2, 8                  // &input[i] → &input[i+2]
	jltu r0, r3, .LBB0_1
```
For some odd reason, register 3 is used both to store the address of the cache and the constant `LOAD_INTO_MRAM - 2`, which adds two additional instructions per iteration.
The total added runtime is about `LOAD_INTO_MRAM` elements ÷ 2 elemens/iteration × 2 instructions/iteration × 11 cycles/iteration ÷ 350000000 cycles/s ≈ 250 ms, which would explain the measured time differences.
At a transfer size of 2048 B, the time difference yielded by this calculation comes down to less than 1 ms, which gets gobbled up by random noise.

I cannot say at the moment why this happens.
At least it seams that this does not pose a problem if was not defined in the same function where the direct memory access happens as the cache address is passed through one of the registers 0 to 7 (https://sdk.upmem.com/2021.3.0/201_DPU_ABI.html).


### One-Way Transfers

*This section was written before ‘An Attempt at an Explanation’.*

What if the data is not supposed to be moved within the MRAM but to be from MRAM to WRAM and the other way around?
`memcpy` is faster when writing to MRAM compared to reading from it, and a static or dynamic allocation of the WRAM cache changes nothing.
For `mram_read` and `mram_write`, the picture is more complicated:
If the cache is dynamically allocated, reading is faster than writing when transfer sizes are small, but it is the opposite when transfer sizes are big, though the lead is minuscule.
Howeven if the cache is statically allocated, writing is always faster, though the lead melts away for bigger transfer sizes.
The interesting part is that the type of allocation bothers only the performance of `mram_read`, not `mram_write`.

Reading from MRAM (static cache):

| Transfer Size | memcpy | mram_read |
| ------------- | -----: | --------: |
|          none |    383 |         / |
|             8 |      / |      1404 |
|            16 |      / |       722 |
|            32 |      / |       393 |
|            64 |      / |       220 |
|           128 |      / |       132 |
|           256 |      / |        75 |
|           512 |      / |        62 |
|          1024 |      / |        56 |
|          2048 |      / |        56 |

Reading from MRAM (dynamic cache):

| Transfer Size | memcpy | mram_read |
| ------------- | -----: | --------: |
|          none |    383 |         / |
|             8 |      / |      1135 |
|            16 |      / |       592 |
|            32 |      / |       355 |
|            64 |      / |       198 |
|           128 |      / |       124 |
|           256 |      / |        75 |
|           512 |      / |        62 |
|          1024 |      / |        56 |
|          2048 |      / |        56 |

Writing to MRAM (static cache):

| Transfer Size | memcpy | mram_write |
| ------------- | -----: | ---------: |
|          none |    332 |          / |
|             8 |      / |       1323 |
|            16 |      / |        670 |
|            32 |      / |        361 |
|            64 |      / |        205 |
|           128 |      / |        126 |
|           256 |      / |         72 |
|           512 |      / |         61 |
|          1024 |      / |         55 |
|          2048 |      / |         55 |

Writing to MRAM (dynamic cache):

| Transfer Size | memcpy | mram_write |
| ------------- | -----: | ---------: |
|          none |    331 |          / |
|             8 |      / |       1319 |
|            16 |      / |        672 |
|            32 |      / |        361 |
|            64 |      / |        205 |
|           128 |      / |        126 |
|           256 |      / |         72 |
|           512 |      / |         61 |
|          1024 |      / |         55 |
|          2048 |      / |         55 |

## Conclusion and Final Remarks

The function `memcpy` trumps `mram_read` and `mram_write` when it comes to usability as no loops, bounds checking and alignment precautions are needed.
When it comes to speed, `memcpy` still holds the upper hand if the transfer sizes are not too big.
It should be noted that `mram_read` and `mram_write` were tested without much overhead.
In some applications, sophisticated logic for checking bounds and guaranteeing alignment may raise the threshold even further.

The little attention `memcpy` receives in the documentation may be off-putting to some.
In fact, all of the advantages listed in ‘Using a String Function’ are purely observations and I give no guarantee for their universality.
An acceptable middleground might be using one of the functions offered by `mram_unaligned.h`, which are at least better documented in their header file—though missing completely from the online documentation.