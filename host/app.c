#include <stdio.h>
#include <stdlib.h>

#include <dpu.h>
#include <dpu_log.h>

static void free_dpus(struct dpu_set_t set) {
    DPU_ASSERT(dpu_free(set));
}

static void alloc_dpus(struct dpu_set_t *set, uint32_t *nr_dpus) {
    DPU_ASSERT(dpu_alloc(1, NULL, set));
    DPU_ASSERT(dpu_load(*set, DPU_BINARY, NULL));
    DPU_ASSERT(dpu_get_nr_dpus(*set, nr_dpus));
}

int main() {
    struct dpu_set_t set, dpu;
    uint32_t nr_dpus;
    alloc_dpus(&set, &nr_dpus);

    DPU_ASSERT(dpu_launch(set, DPU_SYNCHRONOUS));

    DPU_FOREACH(set, dpu) {
        DPU_ASSERT(dpu_log_read(dpu, stdout));
    }

    free_dpus(set);
}