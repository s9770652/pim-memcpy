#include <stddef.h>
#include <stdio.h>

#include <alloc.h>
#include <mram.h>
#include <perfcounter.h>
#include <string.h>

#define LOAD_INTO_MRAM ((1024 * 1024 * 31) / sizeof(int32_t))
#define LOAD_INTO_WRAM ((1024 * 31) / sizeof(int32_t))

int32_t __mram_noinit input[LOAD_INTO_MRAM];
int32_t __mram_noinit output[LOAD_INTO_MRAM];

perfcounter_t cycles;

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

void mram2mram(int32_t *cache) {
    printf("\n\nPERFORMANCE TESTS - MRAM2MRAM (%zu bytes)\n", LOAD_INTO_MRAM * sizeof(int32_t));
    // Using direct accesses.
    cycles = perfcounter_get();
    for (size_t i = 0; i < LOAD_INTO_MRAM; i++) {
        output[i] = input[i];
    }
    cycles = perfcounter_get() - cycles;
    printf("TIME (direct): %7.2f ms\n", (double)cycles / CLOCKS_PER_SEC * 1000);
    printf("\n");

    // Using `memcpy`.
    cycles = perfcounter_get();
    memcpy(output, input, LOAD_INTO_MRAM * sizeof(int32_t));
    cycles = perfcounter_get() - cycles;
    printf("TIME (memcpy): %7.2f ms\n", (double)cycles / CLOCKS_PER_SEC * 1000);

    // Using `memcpy` and a WRAM cache.
    for (size_t block_size = 8; block_size <= 2048; block_size <<= 1) {
        size_t block_length = block_size / sizeof(int32_t);
        cycles = perfcounter_get();
        for (size_t i = 0; i < LOAD_INTO_MRAM; i += block_length) {
            memcpy(cache, &input[i], block_size);
            memcpy(&output[i], cache, block_size);
        }
        cycles = perfcounter_get() - cycles;
        printf("TIME (mc%4d): %7.2f ms\n", block_size, (double)cycles / CLOCKS_PER_SEC * 1000);
    }
    printf("\n");

    // Using `mram_read` and `mram_write` with different transfer sizes up to the maximum of 2048.
    for (size_t block_size = 8; block_size <= 2048; block_size <<= 1) {
        size_t block_length = block_size / sizeof(int32_t);
        cycles = perfcounter_get();
        for (size_t i = 0; i < LOAD_INTO_MRAM; i += block_length) {
            mram_read(&input[i], cache, block_size);
            mram_write(cache, &output[i], block_size);
        }
        cycles = perfcounter_get() - cycles;
        printf("TIME   (%4d): %7.2f ms\n", block_size, (double)cycles / CLOCKS_PER_SEC * 1000);
    }
    printf("\n");
}

void mram2wram(int32_t *cache) {
    printf("\n\nPERFORMANCE TESTS - MRAM2WRAM (%zu bytes)\n", 1024 * LOAD_INTO_MRAM * sizeof(int32_t));
    // Using `memcpy`.
    cycles = perfcounter_get();
    for (int j = 0; j < 1024; j++) {
        memcpy(cache, input, LOAD_INTO_WRAM * sizeof(int32_t));
    }
    cycles = perfcounter_get() - cycles;
    printf("TIME (memcpy): %7.2f ms\n", (double)cycles / CLOCKS_PER_SEC * 1000);

    // Using `mram_read` and `mram_write` with different transfer sizes up to the maximum of 2048.
    for (size_t block_size = 8; block_size <= 2048; block_size <<= 1) {
        size_t block_length = block_size / sizeof(int32_t);
        cycles = perfcounter_get();
        for (int j = 0; j < 1024; j++) {
            for (size_t i = 0; i < LOAD_INTO_WRAM; i += block_length) {
                mram_read(&input[i], cache, block_size);
            }
        }
        cycles = perfcounter_get() - cycles;
        printf("TIME   (%4d): %7.2f ms\n", block_size, (double)cycles / CLOCKS_PER_SEC * 1000);
    }
    printf("\n");
}

void wram2mram(int32_t *cache) {
    printf("\n\nPERFORMANCE TESTS - WRAM2MRAM (%zu bytes)\n", 1024 * LOAD_INTO_MRAM * sizeof(int32_t));
    // Using `memcpy`.
    cycles = perfcounter_get();
    for (int j = 0; j < 1024; j++) {
        memcpy(output, cache, LOAD_INTO_WRAM * sizeof(int32_t));
    }
    cycles = perfcounter_get() - cycles;
    printf("TIME (memcpy): %7.2f ms\n", (double)cycles / CLOCKS_PER_SEC * 1000);

    // Using `mram_read` and `mram_write` with different transfer sizes up to the maximum of 2048.
    for (size_t block_size = 8; block_size <= 2048; block_size <<= 1) {
        size_t block_length = block_size / sizeof(int32_t);
        cycles = perfcounter_get();
        for (int j = 0; j < 1024; j++) {
            for (size_t i = 0; i < LOAD_INTO_WRAM; i += block_length) {
                mram_write(&cache[i], output, block_size);
            }
        }
        cycles = perfcounter_get() - cycles;
        printf("TIME   (%4d): %7.2f ms\n", block_size, (double)cycles / CLOCKS_PER_SEC * 1000);
    }
    printf("\n");
}

int main() {
    // int32_t cache[LOAD_INTO_WRAM];
    int32_t *cache = mem_alloc(LOAD_INTO_WRAM * sizeof(int32_t));
    perfcounter_config(COUNT_CYCLES, true);

    functionality(cache);
    mram2mram(cache);
    // mram2wram(cache);
    // wram2mram(cache);
}