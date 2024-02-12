# Transferring Data Between Different Memories

*The code can be run typing `make run` in the console.
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



## Simplified Access

Though not mentioned at all in the documentation, the file `mram_unaligned.h` is still part of the SDK.
Apart from offering some functions to write atomically to single points in MRMA, it also provides the functions `mram_read_unaligned` and `mram_write_unaligned`, which come with two rules:

* The transfer size must not be greater than 2048 bytes.

* When writing, the WRAM address must have the same alignment as the MRAM address.

The comments state that misalignments are cared for automatically at an additional cost, so that using the plain functions is preferable if misalignments can be avoided.



## Using a String Function

However, there is another barely documented function called `memcpy(to, from, size)`, provided by `string.h`, which also serves to copy data.
My own tests suggests several advantages:

* The transfer size can be arbitrary.
* No address must be aligned on 8 bytes.
* Transfers from one MRAM address to another MRAM address are possible without any intermediate cache in WRAM.


### The Effects of Misalignment

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
On top of that, it may be necessary now to pollute the code with the `__dma_aligned` keyword to even attempt to handle the alignment of WRAM variables.



## Comparing Performance

At first glance, `memcpy` seems to be the best choice.
However, `memcpy` cannot compete with `mram_read` and `mram_write` when it comes to speed, being outperformed at any transfer size except for the smallest one—and even there it is actually slower as the following section ‘Static Versus Dynamic Cache’ is going to show.

In this little test suite, 31 MiB of data are transferred from one MRAM array to another.
The baseline is a simple for loop with element-wise, direct accesses (`output[i] = input[i]`), which takes a total of 2184 ms.
Indeed, this is also the time it takes for `memcpy` to run.
In contrast, `mram_read` and `mram_write` reach this time as well with the smallest transfer size but improve to just 109 ms or 95% less time with the biggest transfer size.
Their unaligned counterparts are exceptionally slow for small transfer sizes but almost catch up for big transfer sizes and beat `memcpy` starting somewhere between 16 B and 32 B.

The performance of `memcpy` cannot be improved by mimicking the behaviour of `mram_read` and `mram_write` by first writing to a WRAM cache and then to the destination in MRAM.
Quite to the contrary, the measured times reach new heights in seemingly erratic order—which do not even stay fully consistent between runs.

The following table contains all runtimes in mili seconds;
the transfer sizes are given in bytes.

| Transfer Size | memcpy | mram_read/write | unaligned |
| ------------- | -----: | --------------: | --------: |
|          none |   2185 |               / |         / |
|             8 |   2616 |            2185 |      5295 |
|            16 |   2407 |            1135 |      2696 |
|            32 |   2269 |             613 |      1417 |
|            64 |   2729 |             356 |       745 |
|           128 |   2467 |             227 |       425 |
|           256 |   4080 |             165 |       257 |
|           512 |   2243 |             129 |       174 |
|          1024 |   2219 |             114 |       134 |
|          2048 |   3753 |             109 |       117 |


### Static Versus Dynamic Cache

For the table above, the cache was allocated statically via `int32_t cache[2048]`.
Curiously, changing to a dynamic allocation via `int32_t *cache = mem_alloc(2048 * sizeof(int32_t))` betters the times of `mram_read` and `mram_write` but worsens the time of `memcpy` with a cache.
The effect first weakens and then disappears with increasing transfer size.
The `unaligned` functions stay unaffected

| Transfer Size | memcpy | mram_read/write | unaligned |
| ------------- | -----: | --------------: | --------: |
|          none |   2180 |               / |         / |
|             8 |   3565 |            1919 |      5295 |
|            16 |   5066 |             994 |      2697 |
|            32 |   5767 |             554 |      1417 |
|            64 |   5224 |             327 |       745 |
|           128 |   4467 |             212 |       425 |
|           256 |   4078 |             155 |       257 |
|           512 |   3891 |             126 |       174 |
|          1024 |   3796 |             113 |       134 |
|          2048 |   3748 |             108 |       117 |


#### An Attempt at an Explanation

##### Aligned Accesses

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


##### Unaligned Accesses

Changing the previous code to use the `unaligned` functions reveals something interesting in the case of static allocation (`r14` holds the address `&output[i]`, `r15` the address `&input[i]`, `r17` the transfer size (which is always 8 B), and `r18` the index `i`):
```
.LBB0_1:
	add r16, r22, -16              // load address of the cache from the stack
	move r0, r15                   // load parameter &input[i]
	move r1, r16                   // load parameter cache
	move r2, r17                   // load parameter 8
	call r23, mram_read_unaligned
	move r0, r16                   // load parameter cache
	move r1, r14                   // load parameter &output[i]
	move r2, r17                   // load parameter 8
	call r23, mram_write_unaligned
	add r18, r18, 2                // i += 2
	add r14, r14, 8                // &output[i] → &output[i+2]
	move r0, 8126462               // LOAD_INTO_MRAM - 2
	add r15, r15, 8                // &input[i] → &input[i+2]
	jltu r18, r0, .LBB0_1
```
As before, two unnecessary loads happen each iteration and, additionally, register 17 is more or less wasted as it holds a constant.
Also, the functions `mram_read_unaligned` and `mram_write_unaligned` do not correspond to assembler instructions and must be explicitely called.
This requires moving the parameters into registers 0 to 2, costing further time.
Theoretically, always loading the number 8 into register 2 is unneeded but happens anyway;
perhaps declaring the third parameter as constant might have yholpen.

While `mram_read`, `mram_write`, and `mram_write_unaligned` return nothing, `mram_read_unaligned` does:
a pointer to the first loaded element in WRAM.
Can this be used to gain a minor performance boost?
Yes!
Using
```
mram_write_unaligned(mram_read_unaligned(&input[i], cache, 8), &output[i], 8);
```
yields
```
.LBB0_1:
	add r1, r22, -16              // load address of the cache from the stack
	move r0, r15                  // load parameter &input[i]
	move r2, r16                  // load parameter 8
	call r23, mram_read_unaligned
	move r1, r14                  // load parameter &output[i]
	move r2, r16                  // load parameter 8
	call r23, mram_write_unaligned
	add r17, r17, 2               // i += 2
	add r14, r14, 8               // &output[i] → &output[i+2]
	move r0, 8126462              // LOAD_INTO_MRAM - 2
	add r15, r15, 8               // &input[i] → &input[i+2]
	jltu r17, r0, .LBB0_1
```
This saves not just one `move` instruction but two since the cache address from the stack is loaded into the correct register directly.
Awesome!
… Although actually measuring the runtime shows no difference at all‽
Perhaps successive `move`s can be coalesced.

Changing to dynamic allocation changes nothing from the above except for dropping the loading of the cache address through an `add`.
At least for the first version with `mram_read_unaligned` outside of `mram_write_unaligned`, this should result in a little performance boost and I am unsure again as to why there is none.


### One-Way Transfers

What if the data is not supposed to be moved within the MRAM but to be from MRAM to WRAM or the other way around?
`memcpy` is faster when writing to MRAM compared to reading from it but overall loses to `mram_read` and `mram_write`.
Remarkably, aligned reading is noticeably faster than writing but just for the transfer sizes 8 B and 16 B whereas unaligned writing is always faster than unaligned reading.
The reasons may be investigated in the future.

Reading from MRAM (dynamic cache):

| Transfer Size | memcpy | mram_read | unaligned |
| ------------- | -----: | --------: | --------: |
|          none |   1524 |         / |         / |
|             8 |      / |      1135 |      2901 |
|            16 |      / |       591 |      1495 |
|            32 |      / |       360 |       790 |
|            64 |      / |       199 |       399 |
|           128 |      / |       124 |       226 |
|           256 |      / |        75 |       136 |
|           512 |      / |        62 |        92 |
|          1024 |      / |        56 |        65 |
|          2048 |      / |        56 |        60 |

Writing to MRAM (dynamic cache):

| Transfer Size | memcpy | mram_write | unaligned |
| ------------- | -----: | ---------: | --------: |
|          none |   1324 |          / |         / |
|             8 |      / |       1323 |      2654 |
|            16 |      / |        672 |      1366 |
|            32 |      / |        361 |       721 |
|            64 |      / |        205 |       396 |
|           128 |      / |        126 |       213 |
|           256 |      / |         72 |       129 |
|           512 |      / |         61 |        88 |
|          1024 |      / |         55 |        63 |
|          2048 |      / |         55 |        58 |



## Conclusion

The function `memcpy` trumps the other functions when it comes to usability as no loops, bounds checking and alignment precautions are needed.
But due to its slowness, `memcpy` should only be used for seldom access to small amounts of data.
It should be noted that `mram_read` and `mram_write` were tested without much overhead.
In some applications, sophisticated logic for checking bounds and guaranteeing alignment may raise the threshold even further;
whether `mram_read_unaligned` and `mram_write_unaligned` provide a benefit in that case should be tested on a case-by-case basis.