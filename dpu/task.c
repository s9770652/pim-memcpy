#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include <alloc.h>
#include <barrier.h>
#include <defs.h>
#include <mram.h>
#include <mram_unaligned.h>
#include <perfcounter.h>

#define LOAD_INTO_MRAM ((1024 * 1024 * 31) / sizeof(int32_t))
#define LOAD_INTO_WRAM ((1024 * 31) / sizeof(int32_t))

int32_t __mram_noinit input[LOAD_INTO_MRAM];
int32_t __mram_noinit output[LOAD_INTO_MRAM];
perfcounter_t cycles[NR_TASKLETS];

BARRIER_INIT(omni_barrier, NR_TASKLETS);


inline double get_max_time(void) {
    perfcounter_t time = cycles[0];
    #pragma unroll
    for (size_t i = 1; i < NR_TASKLETS; i++) {
        time = time < cycles[i] ? cycles[i] : time;;
    }
    return (double)time / CLOCKS_PER_SEC * 1000;
}

inline double get_total_time(void) {
    perfcounter_t time = cycles[0];
    #pragma unroll
    for (size_t i = 1; i < NR_TASKLETS; i++) {
        time += cycles[i];
    }
    return (double)time / CLOCKS_PER_SEC * 1000;
}

// inline double get_time(void) { return get_total_time(); }
inline double get_time(void) { return get_max_time(); }

void init_array(int32_t __mram_ptr *array, size_t n, int32_t offset) {
    for (size_t i = 0; i < n; i++) {
        array[i] = i + offset;
    }
}

void print_array(int32_t __mram_ptr *array, size_t n) {
    for (size_t i = 0; i < n; i++) {
        printf("%3d ", array[i]);
    }
    printf("\n");
}

void functionality(int32_t *cache) {
    printf("FUNCTIONALITY TESTS\n\n");
    size_t const n = 17;
    for (size_t i = 0; i < n + 1; i++) cache[i] = i;
    init_array(output, n, 100);
    printf("MRAM array originally:\n");
    print_array(output, n);

    printf("\nmemcpy with %d bytes (%d elements); unaligned MRAM address:\n", (n - 1) * sizeof(int32_t), n - 1);
    memcpy(&output[1], cache, (n - 1) * sizeof(int32_t));
    print_array(output, n);

    printf("\nmemcpy with %d bytes (%d elements); unaligned MRAM address:\n", (n - 2) * sizeof(int32_t), n - 2);
    init_array(output, n, 100);
    memcpy(&output[1], cache, (n - 2) * sizeof(int32_t));
    print_array(output, n);

    printf("\nmram_write with %d bytes (%d elements); unaligned MRAM address:\n", (n - 1) * sizeof(int32_t), n - 1);
    init_array(output, n, 100);
    mram_write(cache, &output[1], (n - 1) * sizeof(int32_t));
    print_array(output, n);

    printf("\nmram_write with %d bytes (%d elements); unaligned WRAM address:\n", (n - 1) * sizeof(int32_t), n - 1);
    init_array(output, n, 100);
    mram_write(&cache[1], output, (n - 1) * sizeof(int32_t));
    print_array(output, n);
}

void mram2mram(int32_t * const cache, size_t const start, size_t const end, size_t const len) {
    if (me() == 0) printf("\n\nPERFORMANCE TESTS - MRAM2MRAM (%zu bytes)\n", LOAD_INTO_MRAM * sizeof(int32_t));
    // Using direct accesses.
    cycles[me()] = perfcounter_get();
    for (size_t i = start; i < end; i++) {
        output[i] = input[i];
    }
    cycles[me()] = perfcounter_get() - cycles[me()];
    if (me() == 0) printf("TIME (direct): %8.2f ms\n", get_time());
    barrier_wait(&omni_barrier);

    // Using `memcpy`.
    cycles[me()] = perfcounter_get();
    memcpy(&output[start], &input[start], len * sizeof(int32_t));
    cycles[me()] = perfcounter_get() - cycles[me()];
    if (me() == 0) printf("TIME (memcpy): %8.2f ms\n", get_time());
    barrier_wait(&omni_barrier);

    // // Using `memcpy` and a WRAM cache.
    // for (size_t block_size = 8; block_size <= 2048; block_size <<= 1) {
    //     size_t block_length = block_size / sizeof(int32_t);
    //     cycles[me()] = perfcounter_get();
    //     for (size_t i = 0; i < LOAD_INTO_MRAM; i += block_length) {
    //         memcpy(cache, &input[i], block_size);
    //         memcpy(&output[i], cache, block_size);
    //     }
    //     cycles[me()] = perfcounter_get() - cycles[me()];
    //     barrier_wait(&omni_barrier);
    //     if (me() == 0) print_time(cycles, "memcpy");
    //     printf("TIME (mc%4d): %7.2f ms\n", block_size, (double)cycles[me()] / CLOCKS_PER_SEC * 1000);
    // }
    // printf("\n");

    // Using `mram_read` and `mram_write` with different transfer sizes up to the maximum of 2048.
    for (size_t block_size = 8; block_size <= 2048; block_size <<= 1) {
        size_t block_length = block_size / sizeof(int32_t);
        cycles[me()] = perfcounter_get();
        for (size_t i = start; i < end; i += block_length) {
            mram_read(&input[i], cache, block_size);
            mram_write(cache, &output[i], block_size);
        }
        cycles[me()] = perfcounter_get() - cycles[me()];
        if (me() == 0) printf("TIME (a %4d): %8.2f ms\n", block_size, get_time());
        barrier_wait(&omni_barrier);
    }
    if (me() == 0) printf("\n");

    // // Using `mram_read_unaligned` and `mram_write_unaligned` with different transfer sizes up to the maximum of 2048.
    for (size_t block_size = 8; block_size <= 2048; block_size <<= 1) {
        size_t block_length = block_size / sizeof(int32_t);
        cycles[me()] = perfcounter_get();
        for (size_t i = start; i < end; i += block_length) {
            mram_read_unaligned(&input[i], cache, block_size);
            mram_write_unaligned(cache, &output[i], block_size);
        }
        cycles[me()] = perfcounter_get() - cycles[me()];
        if (me() == 0) printf("TIME (u %4d): %8.2f ms\n", block_size, get_time());
        barrier_wait(&omni_barrier);
    }
    if (me() == 0) printf("\n");
}

void mram2wram(int32_t *cache) {
    if (NR_TASKLETS > 0) {
        printf("mram2wram is not yet adapted to multi threading!");
        return;
    }
    printf("\n\nPERFORMANCE TESTS - MRAM2WRAM (%zu bytes)\n", 1024 * LOAD_INTO_MRAM * sizeof(int32_t));
    // Using `memcpy`.
    cycles[me()] = perfcounter_get();
    for (int j = 0; j < 1024; j++) {
        memcpy(cache, input, LOAD_INTO_WRAM * sizeof(int32_t));
    }
    cycles[me()] = perfcounter_get() - cycles[me()];
    printf("TIME (memcpy): %7.2f ms\n", (double)cycles[me()] / CLOCKS_PER_SEC * 1000);

    // Using `mram_read` and `mram_write` with different transfer sizes up to the maximum of 2048.
    for (size_t block_size = 8; block_size <= 2048; block_size <<= 1) {
        size_t block_length = block_size / sizeof(int32_t);
        cycles[me()] = perfcounter_get();
        for (int j = 0; j < 1024; j++) {
            for (size_t i = 0; i < LOAD_INTO_WRAM; i += block_length) {
                mram_read(&input[i], cache, block_size);
            }
        }
        cycles[me()] = perfcounter_get() - cycles[me()];
        printf("TIME (a %4d): %7.2f ms\n", block_size, (double)cycles[me()] / CLOCKS_PER_SEC * 1000);
    }
    printf("\n");

    // Using `mram_read_unaligned` and `mram_write_unaligned` with different transfer sizes up to the maximum of 2048.
    for (size_t block_size = 8; block_size <= 2048; block_size <<= 1) {
        size_t block_length = block_size / sizeof(int32_t);
        cycles[me()] = perfcounter_get();
        for (int j = 0; j < 1024; j++) {
            for (size_t i = 0; i < LOAD_INTO_WRAM; i += block_length) {
                mram_read_unaligned(&input[i], cache, block_size);
            }
        }
        cycles[me()] = perfcounter_get() - cycles[me()];
        printf("TIME (u %4d): %7.2f ms\n", block_size, (double)cycles[me()] / CLOCKS_PER_SEC * 1000);
    }
    printf("\n");
}

void wram2mram(int32_t *cache) {
    if (NR_TASKLETS > 0) {
        printf("wram2mram is not yet adapted to multi threading!");
        return;
    }
    printf("\n\nPERFORMANCE TESTS - WRAM2MRAM (%zu bytes)\n", 1024 * LOAD_INTO_MRAM * sizeof(int32_t));
    // Using `memcpy`.
    cycles[me()] = perfcounter_get();
    for (int j = 0; j < 1024; j++) {
        memcpy(output, cache, LOAD_INTO_WRAM * sizeof(int32_t));
    }
    cycles[me()] = perfcounter_get() - cycles[me()];
    printf("TIME (memcpy): %7.2f ms\n", (double)cycles[me()] / CLOCKS_PER_SEC * 1000);

    // Using `mram_read` and `mram_write` with different transfer sizes up to the maximum of 2048.
    for (size_t block_size = 8; block_size <= 2048; block_size <<= 1) {
        size_t block_length = block_size / sizeof(int32_t);
        cycles[me()] = perfcounter_get();
        for (int j = 0; j < 1024; j++) {
            for (size_t i = 0; i < LOAD_INTO_WRAM; i += block_length) {
                mram_write(&cache[i], output, block_size);
            }
        }
        cycles[me()] = perfcounter_get() - cycles[me()];
        printf("TIME (a %4d): %7.2f ms\n", block_size, (double)cycles[me()] / CLOCKS_PER_SEC * 1000);
    }
    printf("\n");

    // Using `mram_read_unaligned` and `mram_write_unaligned` with different transfer sizes up to the maximum of 2048.
    for (size_t block_size = 8; block_size <= 2048; block_size <<= 1) {
        size_t block_length = block_size / sizeof(int32_t);
        cycles[me()] = perfcounter_get();
        for (int j = 0; j < 1024; j++) {
            for (size_t i = 0; i < LOAD_INTO_WRAM; i += block_length) {
                mram_write_unaligned(&cache[i], output, block_size);
            }
        }
        cycles[me()] = perfcounter_get() - cycles[me()];
        printf("TIME (u %4d): %7.2f ms\n", block_size, (double)cycles[me()] / CLOCKS_PER_SEC * 1000);
    }
    printf("\n");
}

int main() {
    // int32_t cache[LOAD_INTO_WRAM];
    int32_t * const cache = mem_alloc(LOAD_INTO_WRAM / NR_TASKLETS * sizeof(int32_t));
    perfcounter_config(COUNT_CYCLES, true);

    size_t const len = (LOAD_INTO_MRAM / NR_TASKLETS) ^ 1;  // ugly fix to avoid alignment issues
    // functionality(cache);
    mram2mram(cache, len * me(), len * (me() + 1), len);
    // mram2wram(cache);
    // wram2mram(cache);
}