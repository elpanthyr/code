#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// Scalar reference 
static inline int16_t sat_q15_scalar(int32_t v) {
    if (v >  32767) return  32767;
    if (v < -32768) return -32768;
    return (int16_t)v;
}

void q15_axpy_ref(const int16_t *a, const int16_t *b,
                  int16_t *y, int n, int16_t alpha)
{
    for (int i = 0; i < n; ++i) {
        int32_t acc = (int32_t)a[i] + (int32_t)alpha * (int32_t)b[i];
        y[i] = sat_q15_scalar(acc);
    }
}

// RVV include per ratified v1.0 spec 
#if __riscv_v_intrinsic >= 1000000
  #include <riscv_vector.h>  // v1.0 test macro & header inclusion
#endif

// RVV implementation
void q15_axpy_rvv(const int16_t *a, const int16_t *b,
                  int16_t *y, int n, int16_t alpha)
{
#if !defined(__riscv) || !defined(__riscv_vector) || (__riscv_v_intrinsic < 1000000)
    // Fallback (keeps correctness off-target)
    q15_axpy_ref(a, b, y, n, alpha);
#else
    size_t vl;
    // VLA loop structure ensures compatibility with any hardware VLEN
    for (; n > 0; n -= vl, a += vl, b += vl, y += vl) {
        // e16m1: 16-bit elements with LMUL=1
        vl = __riscv_vsetvl_e16m1(n);

        // Load vectors a and b
        vint16m1_t va = __riscv_vle16_v_i16m1(a, vl);
        vint16m1_t vb = __riscv_vle16_v_i16m1(b, vl);

        // Widen a to 32-bit and multiply-accumulate with widened (alpha * b)
        // ensures intermediate precision matches scalar reference
        vint32m2_t v_acc = __riscv_vwadd_vx_i32m2(va, 0, vl);
        v_acc = __riscv_vwmacc_vx_i32m2(v_acc, alpha, vb, vl);

        // Saturating narrowing using vnclip with rounding mode RNE (Round to Nearest, ties to Even)
        // guarantees bit-perfect Q15 saturation
        vint16m1_t v_res = __riscv_vnclip_wx_i16m1(v_acc, 0, __RISCV_VXRM_RNE, vl);

        // Store result vector back to y
        __riscv_vse16_v_i16m1(y, v_res, vl);
    }
#endif
}

// Verification & benchmark
static int verify_equal(const int16_t *ref, const int16_t *test, int n, int32_t *max_diff) {
    int ok = 1;
    int32_t md = 0;
    for (int i = 0; i < n; ++i) {
        int32_t d = (int32_t)ref[i] - (int32_t)test[i];
        if (d < 0) d = -d;
        if (d > md) md = d;
        if (d != 0) ok = 0;
    }
    *max_diff = md;
    return ok;
}

#if defined(__riscv)
static inline uint64_t rdcycle(void) { uint64_t c; asm volatile ("rdcycle %0" : "=r"(c)); return c; }
#endif

int main(void) {
    int ok = 1;
    const int N = 4096;
    int16_t *a  = (int16_t*)aligned_alloc(64, N * sizeof(int16_t));
    int16_t *b  = (int16_t*)aligned_alloc(64, N * sizeof(int16_t));
    int16_t *y0 = (int16_t*)aligned_alloc(64, N * sizeof(int16_t));
    int16_t *y1 = (int16_t*)aligned_alloc(64, N * sizeof(int16_t));

    if (!a || !b || !y0 || !y1) return 1;

    // Deterministic integer data
    srand(1234);
    for (int i = 0; i < N; ++i) {
        a[i] = (int16_t)((rand() % 65536) - 32768);
        b[i] = (int16_t)((rand() % 65536) - 32768);
    }

    const int16_t alpha = 3;

#if defined(__riscv)
    uint64_t c0_ref = rdcycle();
    q15_axpy_ref(a, b, y0, N, alpha);
    uint64_t c1_ref = rdcycle();
    printf("Cycles ref: %llu\n", (unsigned long long)(c1_ref - c0_ref));

    int32_t md = 0;
    uint64_t c0_rvv = rdcycle();
    q15_axpy_rvv(a, b, y1, N, alpha);
    uint64_t c1_rvv = rdcycle();
    
    ok = verify_equal(y0, y1, N, &md);
    printf("Verify RVV: %s (max diff = %d)\n", ok ? "OK" : "FAIL", md);
    printf("Cycles RVV: %llu\n", (unsigned long long)(c1_rvv - c0_rvv));
    printf("Speedup: %.2fx\n", (double)(c1_ref - c0_ref) / (c1_rvv - c0_rvv));
#else
    q15_axpy_ref(a, b, y0, N, alpha);
    q15_axpy_rvv(a, b, y1, N, alpha);
    int32_t md = 0;
    ok = verify_equal(y0, y1, N, &md);
    printf("Non-RISCV Verification: %s\n", ok ? "OK" : "FAIL");
#endif

    free(a); free(b); free(y0); free(y1);
    return ok ? 0 : 1;
}