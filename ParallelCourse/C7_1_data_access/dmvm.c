#include "util.h"

#include <immintrin.h>

#define indexA(r,c) ((r) + (c) * (lda)) 

#ifndef ROW_TILING
#define ROW_TILING 10000
#endif

void data_init(double** A_p, double** x_p, double** y_p, uint64_t row, uint64_t col){
    *A_p = (double*)aligned_alloc(64, row * col * sizeof(double));
    *x_p = (double*)aligned_alloc(64, col * sizeof(double));
    *y_p = (double*)aligned_alloc(64, row * sizeof(double));
    
    double* A = *A_p;
    double* x = *x_p;
    double* y = *y_p;

    for(uint64_t i = 0; i < row * col; ++i){
        A[i] = 1.;
    }
    for(uint64_t i = 0; i < col; ++i){
        x[i] = 1.;
    }
    for(uint64_t i = 0; i < row; ++i){
        y[i] = 1.;
    }
}

void data_release(double* A, double* x, double* y){
    free(A);
    free(x);
    free(y);
}

double dmvm_data_traffic(uint64_t row, uint64_t col, FILE* output_file){
    uint64_t mem_2_L3, L3_2_L2, L2_2_L1;
    mem_2_L3 = 8;
    L3_2_L2 = 8;
    L2_2_L1 = 8;
    if(row > (L1_CACHE_SIZE / sizeof(double) / 2)){ // 3072
        L2_2_L1 += 16;
    }
    if(row > (L2_CACHE_SIZE / sizeof(double) / 2)){ // 81920
        L3_2_L2 += 16;
    }
    if(row > (L3_CACHE_SIZE / sizeof(double) / 2)){ // 3538944
        mem_2_L3 += 16;
    }
    fprintf(output_file, "%ld %ld %ld %ld\n", row, L2_2_L1, L3_2_L2, mem_2_L3);
    fflush(output_file);
}

double dmvm_kernel_naive(uint64_t row, uint64_t col, double* A, uint64_t lda, double* x, double* y, uint64_t repeat_count, FILE* output_file){
    // warm up
    for(uint64_t c = 0; c < col; ++c){
        const double xValue = x[c];
        const double* A_col = &A[c * lda];
        for(uint64_t r = 0; r < row; ++r){
            y[r] += A_col[r] * xValue;
        }
    }
    double time_start = dtime();
    uint64_t cycle_start = rdtsc();
    for(uint64_t repeat = 0; repeat < repeat_count; ++repeat){
        for(uint64_t c = 0; c < col; ++c){
            const double xValue = x[c];
            const double* A_col = &A[c * lda];
            for(uint64_t r = 0; r < row; ++r){
                y[r] += A_col[r] * xValue;
            }
        }
    }
    uint64_t cycle_end = rdtsc();
    double time_end = dtime();
    double time = time_end - time_start;
    uint64_t cycles = cycle_end - cycle_start;

    double MFLOPS =  2. * repeat_count * row * col / 1e6 / time;
    double MBPS = 4. * 8. * repeat_count * row * col / 1e6 / time;
    
    printf("cycle : %ld\n", cycles);
    printf("time : %e\n", time);
    printf("frequent : %.2lf GHz\n", cycles / 1e9 / time);
    
    printf("repeat count : %ld\n", repeat_count);
    printf("performance : %8.4lf MFLOPS\n", MFLOPS);
    printf("bandwidth : %8.4lf MB/s\n", MBPS);
    fflush(stdout);

    fprintf(output_file, "%ld %8.4lf\n", row, MFLOPS);
    fflush(output_file);
    
    // cheat compiler
    double sum = 0.;
    for(uint64_t i = 0; i < row; ++i){
        sum += y[i];
    }
    return sum;
}

double dmvm_kernel_unroll2(uint64_t row, uint64_t col, double* A, uint64_t lda, double* x, double* y, uint64_t repeat_count, FILE* output_file){
    // warm up
    for(uint64_t c = 0; c < col; c += 2){
        const double xValue0 = x[c];
        const double xValue1 = x[c+1];
        const double* A_col0 = &A[c * lda];
        const double* A_col1 = &A[(c + 1) * lda];
        for(uint64_t r = 0; r < row; ++r){
            y[r] += A_col0[r] * xValue0;
            y[r] += A_col1[r] * xValue1;
        }
    }
    double time_start = dtime();
    uint64_t cycle_start = rdtsc();
    for(uint64_t repeat = 0; repeat < repeat_count; ++repeat){
        for(uint64_t c = 0; c < col; c += 2){
            const double xValue0 = x[c];
            const double xValue1 = x[c+1];
            const double* A_col0 = &A[c * lda];
            const double* A_col1 = &A[(c + 1) * lda];
            for(uint64_t r = 0; r < row; ++r){
                y[r] += A_col0[r] * xValue0;
                y[r] += A_col1[r] * xValue1;
            }
        }
    }
    uint64_t cycle_end = rdtsc();
    double time_end = dtime();
    double time = time_end - time_start;
    uint64_t cycles = cycle_end - cycle_start;

    double MFLOPS =  2. * repeat_count * row * col / 1e6 / time;
    double MBPS = 4. * 8. * repeat_count * row * col / 1e6 / time;
    
    printf("cycle : %ld\n", cycles);
    printf("time : %e\n", time);
    printf("frequent : %.2lf GHz\n", cycles / 1e9 / time);
    
    printf("repeat count : %ld\n", repeat_count);
    printf("performance : %8.4lf MFLOPS\n", MFLOPS);
    printf("bandwidth : %8.4lf MB/s\n", MBPS);
    fflush(stdout);

    fprintf(output_file, "%ld %8.4lf\n", row, MFLOPS);
    fflush(output_file);
    
    // cheat compiler
    double sum = 0.;
    for(uint64_t i = 0; i < row; ++i){
        sum += y[i];
    }
    return sum;
}

double dmvm_kernel_tiling(uint64_t row, uint64_t col, double* A, uint64_t lda, double* x, double* y, uint64_t repeat_count, FILE* output_file){

    // warm up
    for(uint64_t rbs = 0; rbs < row; rbs += ROW_TILING){
        uint64_t rbe = min(rbs + ROW_TILING, row);
        for(uint64_t c = 0; c < col; ++c){
            const double xValue0 = x[c];
            const double* A_col0 = &A[c * lda];
            for(uint64_t r = rbs; r < rbe; ++r){
                y[r] += A_col0[r] * xValue0;
            }
        }
    }

    double time_start = dtime();
    uint64_t cycle_start = rdtsc();
    for(uint64_t repeat = 0; repeat < repeat_count; ++repeat){
        for(uint64_t rbs = 0; rbs < row; rbs += ROW_TILING){
            uint64_t rbe = min(rbs + ROW_TILING, row);
            for(uint64_t c = 0; c < col; ++c){
                const double xValue0 = x[c];
                const double* A_col0 = &A[c * lda];
                for(uint64_t r = rbs; r < rbe; ++r){
                    y[r] += A_col0[r] * xValue0;
                }
            }
        }
    }
    uint64_t cycle_end = rdtsc();
    double time_end = dtime();
    double time = time_end - time_start;
    uint64_t cycles = cycle_end - cycle_start;

    double MFLOPS =  2. * repeat_count * row * col / 1e6 / time;
    double MBPS = 4. * 8. * repeat_count * row * col / 1e6 / time;
    
    printf("cycle : %ld\n", cycles);
    printf("time : %e\n", time);
    printf("frequent : %.2lf GHz\n", cycles / 1e9 / time);
    
    printf("repeat count : %ld\n", repeat_count);
    printf("performance : %8.4lf MFLOPS\n", MFLOPS);
    printf("bandwidth : %8.4lf MB/s\n", MBPS);
    fflush(stdout);

    fprintf(output_file, "%ld %8.4lf\n", row, MFLOPS);
    fflush(output_file);
    
    // cheat compiler
    double sum = 0.;
    for(uint64_t i = 0; i < row; ++i){
        sum += y[i];
    }
    return sum;
}

double dmvm_kernel_avx512(uint64_t row, uint64_t col, double* A, uint64_t lda, double* x, double* y, uint64_t repeat_count, FILE* output_file){
    // warm up
    for(uint64_t c = 0; c < col; ++c){
        const double xValue = x[c];
        const double* A_col = &A[c * lda];
        __m512d vx = _mm512_set1_pd(xValue);
        for(uint64_t r = 0; r < row; r += 16){
            __m512d vA00 = _mm512_load_pd(A_col + r);
            __m512d vA10= _mm512_load_pd(A_col + r);
            __m512d vy0 = _mm512_load_pd(y + r);
            __m512d vy1 = _mm512_load_pd(y + r + 8);
            vy0 = _mm512_fmadd_pd(vA00, vx, vy0);
            vy1 = _mm512_fmadd_pd(vA10, vx, vy1);
            _mm512_store_pd(y + r, vy0);
            _mm512_store_pd(y + r + 8, vy1);
        }
    }
    double time_start = dtime();
    uint64_t cycle_start = rdtsc();
    for(uint64_t repeat = 0; repeat < repeat_count; ++repeat){
        for(uint64_t c = 0; c < col; ++c){
            const double xValue = x[c];
            const double* A_col = &A[c * lda];
            __m512d vx = _mm512_set1_pd(xValue);
            for(uint64_t r = 0; r < row; r += 16){
                __m512d vA00 = _mm512_load_pd(A_col + r);
                __m512d vA10= _mm512_load_pd(A_col + r);
                __m512d vy0 = _mm512_load_pd(y + r);
                __m512d vy1 = _mm512_load_pd(y + r + 8);
                vy0 = _mm512_fmadd_pd(vA00, vx, vy0);
                vy1 = _mm512_fmadd_pd(vA10, vx, vy1);
                _mm512_store_pd(y + r, vy0);
                _mm512_store_pd(y + r + 8, vy1);
            }
        }
    }
    uint64_t cycle_end = rdtsc();
    double time_end = dtime();
    double time = time_end - time_start;
    uint64_t cycles = cycle_end - cycle_start;

    double MFLOPS =  2. * repeat_count * row * col / 1e6 / time;
    double MBPS = 4. * 8. * repeat_count * row * col / 1e6 / time;
    
    printf("cycle : %ld\n", cycles);
    printf("time : %e\n", time);
    printf("frequent : %.2lf GHz\n", cycles / 1e9 / time);
    
    printf("repeat count : %ld\n", repeat_count);
    printf("performance : %8.4lf MFLOPS\n", MFLOPS);
    printf("bandwidth : %8.4lf MB/s\n", MBPS);
    fflush(stdout);

    fprintf(output_file, "%ld %8.4lf\n", row, MFLOPS);
    fflush(output_file);
    
    // cheat compiler
    double sum = 0.;
    for(uint64_t i = 0; i < row; ++i){
        sum += y[i];
    }
    return sum;
}

double dmvm_kernel_avx512_unroll2(uint64_t row, uint64_t col, double* A, uint64_t lda, double* x, double* y, uint64_t repeat_count, FILE* output_file){
    // warm up
    for(uint64_t c = 0; c < col; c += 2){
        const double xValue0 = x[c];
        const double xValue1 = x[c+1];
        const double* A_col0 = &A[c * lda];
        const double* A_col1 = &A[(c+1) * lda];
        __m512d vx0 = _mm512_set1_pd(xValue0);
        __m512d vx1 = _mm512_set1_pd(xValue1);
        for(uint64_t r = 0; r < row; r += 16){
            __m512d vA00 = _mm512_load_pd(A_col0 + r);
            __m512d vA01 = _mm512_load_pd(A_col1 + r);
            __m512d vA10= _mm512_load_pd(A_col0 + r + 8);
            __m512d vA11= _mm512_load_pd(A_col1 + r + 8);
            __m512d vy0 = _mm512_load_pd(y + r);
            __m512d vy1 = _mm512_load_pd(y + r + 8);
            vy0 = _mm512_fmadd_pd(vA00, vx0, vy0);
            vy1 = _mm512_fmadd_pd(vA10, vx0, vy1);
            vy0 = _mm512_fmadd_pd(vA01, vx1, vy0);
            vy1 = _mm512_fmadd_pd(vA11, vx1, vy1);
            _mm512_store_pd(y + r, vy0);
            _mm512_store_pd(y + r + 8, vy1);
        }
    }
    double time_start = dtime();
    uint64_t cycle_start = rdtsc();
    for(uint64_t repeat = 0; repeat < repeat_count; ++repeat){
        for(uint64_t c = 0; c < col; c += 2){
            const double xValue0 = x[c];
            const double xValue1 = x[c+1];
            const double* A_col0 = &A[c * lda];
            const double* A_col1 = &A[(c+1) * lda];
            __m512d vx0 = _mm512_set1_pd(xValue0);
            __m512d vx1 = _mm512_set1_pd(xValue1);
            for(uint64_t r = 0; r < row; r += 16){
                __m512d vA00 = _mm512_load_pd(A_col0 + r);
                __m512d vA01 = _mm512_load_pd(A_col1 + r);
                __m512d vA10= _mm512_load_pd(A_col0 + r + 8);
                __m512d vA11= _mm512_load_pd(A_col1 + r + 8);
                __m512d vy0 = _mm512_load_pd(y + r);
                __m512d vy1 = _mm512_load_pd(y + r + 8);
                vy0 = _mm512_fmadd_pd(vA00, vx0, vy0);
                vy1 = _mm512_fmadd_pd(vA10, vx0, vy1);
                vy0 = _mm512_fmadd_pd(vA01, vx1, vy0);
                vy1 = _mm512_fmadd_pd(vA11, vx1, vy1);
                _mm512_store_pd(y + r, vy0);
                _mm512_store_pd(y + r + 8, vy1);
            }
        }
    }
    uint64_t cycle_end = rdtsc();
    double time_end = dtime();
    double time = time_end - time_start;
    uint64_t cycles = cycle_end - cycle_start;

    double MFLOPS =  2. * repeat_count * row * col / 1e6 / time;
    double MBPS = 4. * 8. * repeat_count * row * col / 1e6 / time;
    
    printf("cycle : %ld\n", cycles);
    printf("time : %e\n", time);
    printf("frequent : %.2lf GHz\n", cycles / 1e9 / time);
    
    printf("repeat count : %ld\n", repeat_count);
    printf("performance : %8.4lf MFLOPS\n", MFLOPS);
    printf("bandwidth : %8.4lf MB/s\n", MBPS);
    fflush(stdout);

    fprintf(output_file, "%ld %8.4lf\n", row, MFLOPS);
    fflush(output_file);
    
    // cheat compiler
    double sum = 0.;
    for(uint64_t i = 0; i < row; ++i){
        sum += y[i];
    }
    return sum;
}

double dmvm_kernel_avx512_unroll4(uint64_t row, uint64_t col, double* A, uint64_t lda, double* x, double* y, uint64_t repeat_count, FILE* output_file){
    // warm up
    for(uint64_t c = 0; c < col; c += 4){
        const double xValue0 = x[c];
        const double xValue1 = x[c+1];
        const double xValue2 = x[c+2];
        const double xValue3 = x[c+3];
        const double* A_col0 = &A[c * lda];
        const double* A_col1 = &A[(c+1) * lda];
        const double* A_col2 = &A[(c+2) * lda];
        const double* A_col3 = &A[(c+3) * lda];
        __m512d vx0 = _mm512_set1_pd(xValue0);
        __m512d vx1 = _mm512_set1_pd(xValue1);
        __m512d vx2 = _mm512_set1_pd(xValue2);
        __m512d vx3 = _mm512_set1_pd(xValue3);
        for(uint64_t r = 0; r < row; r += 16){
            __m512d vA00 = _mm512_load_pd(A_col0 + r);
            __m512d vA01 = _mm512_load_pd(A_col1 + r);
            __m512d vA02 = _mm512_load_pd(A_col2 + r);
            __m512d vA03 = _mm512_load_pd(A_col3 + r);
            __m512d vA10 = _mm512_load_pd(A_col0 + r + 8);
            __m512d vA11 = _mm512_load_pd(A_col1 + r + 8);
            __m512d vA12 = _mm512_load_pd(A_col2 + r + 8);
            __m512d vA13 = _mm512_load_pd(A_col3 + r + 8);
            __m512d vy0 = _mm512_load_pd(y + r);
            __m512d vy1 = _mm512_load_pd(y + r + 8);
            vy0 = _mm512_fmadd_pd(vA00, vx0, vy0);
            vy1 = _mm512_fmadd_pd(vA10, vx0, vy1);
            vy0 = _mm512_fmadd_pd(vA01, vx1, vy0);
            vy1 = _mm512_fmadd_pd(vA11, vx1, vy1);
            vy0 = _mm512_fmadd_pd(vA02, vx2, vy0);
            vy1 = _mm512_fmadd_pd(vA12, vx2, vy1);
            vy0 = _mm512_fmadd_pd(vA03, vx3, vy0);
            vy1 = _mm512_fmadd_pd(vA13, vx3, vy1);
            _mm512_store_pd(y + r, vy0);
            _mm512_store_pd(y + r + 8, vy1);
        }
    }
    double time_start = dtime();
    uint64_t cycle_start = rdtsc();
    for(uint64_t repeat = 0; repeat < repeat_count; ++repeat){
        for(uint64_t c = 0; c < col; c += 4){
            const double xValue0 = x[c];
            const double xValue1 = x[c+1];
            const double xValue2 = x[c+2];
            const double xValue3 = x[c+3];
            const double* A_col0 = &A[c * lda];
            const double* A_col1 = &A[(c+1) * lda];
            const double* A_col2 = &A[(c+2) * lda];
            const double* A_col3 = &A[(c+3) * lda];
            __m512d vx0 = _mm512_set1_pd(xValue0);
            __m512d vx1 = _mm512_set1_pd(xValue1);
            __m512d vx2 = _mm512_set1_pd(xValue2);
            __m512d vx3 = _mm512_set1_pd(xValue3);
            for(uint64_t r = 0; r < row; r += 16){
                __m512d vA00 = _mm512_load_pd(A_col0 + r);
                __m512d vA01 = _mm512_load_pd(A_col1 + r);
                __m512d vA02 = _mm512_load_pd(A_col2 + r);
                __m512d vA03 = _mm512_load_pd(A_col3 + r);
                __m512d vA10 = _mm512_load_pd(A_col0 + r + 8);
                __m512d vA11 = _mm512_load_pd(A_col1 + r + 8);
                __m512d vA12 = _mm512_load_pd(A_col2 + r + 8);
                __m512d vA13 = _mm512_load_pd(A_col3 + r + 8);
                __m512d vy0 = _mm512_load_pd(y + r);
                __m512d vy1 = _mm512_load_pd(y + r + 8);
                vy0 = _mm512_fmadd_pd(vA00, vx0, vy0);
                vy1 = _mm512_fmadd_pd(vA10, vx0, vy1);
                vy0 = _mm512_fmadd_pd(vA01, vx1, vy0);
                vy1 = _mm512_fmadd_pd(vA11, vx1, vy1);
                vy0 = _mm512_fmadd_pd(vA02, vx2, vy0);
                vy1 = _mm512_fmadd_pd(vA12, vx2, vy1);
                vy0 = _mm512_fmadd_pd(vA03, vx3, vy0);
                vy1 = _mm512_fmadd_pd(vA13, vx3, vy1);
                _mm512_store_pd(y + r, vy0);
                _mm512_store_pd(y + r + 8, vy1);
            }
        }
    }
    uint64_t cycle_end = rdtsc();
    double time_end = dtime();
    double time = time_end - time_start;
    uint64_t cycles = cycle_end - cycle_start;

    double MFLOPS =  2. * repeat_count * row * col / 1e6 / time;
    double MBPS = 4. * 8. * repeat_count * row * col / 1e6 / time;
    
    printf("cycle : %ld\n", cycles);
    printf("time : %e\n", time);
    printf("frequent : %.2lf GHz\n", cycles / 1e9 / time);
    
    printf("repeat count : %ld\n", repeat_count);
    printf("performance : %8.4lf MFLOPS\n", MFLOPS);
    printf("bandwidth : %8.4lf MB/s\n", MBPS);
    fflush(stdout);

    fprintf(output_file, "%ld %8.4lf\n", row, MFLOPS);
    fflush(output_file);
    
    // cheat compiler
    double sum = 0.;
    for(uint64_t i = 0; i < row; ++i){
        sum += y[i];
    }
    return sum;
}

void dmvm_test(
    uint64_t row,
    uint64_t col,
    uint64_t flops_per_test,
    FILE* output_file)
{
    uint64_t adapative_repeat_count = flops_per_test / row / col;

    double *A, *x, *y;
    uint64_t lda = row;
    data_init(&A, &x, &y, row, col);


#if defined(DMVM_NAIVE)
    dmvm_kernel_naive(row, col, A, lda, x, y, adapative_repeat_count, output_file);
#elif defined(DMVM_UNROLL2)
    dmvm_kernel_unroll2(row, col, A, lda, x, y, adapative_repeat_count, output_file);
#elif defined(DMVM_TILING)
    dmvm_kernel_tiling(row, col, A, lda, x, y, adapative_repeat_count, output_file);
#elif defined(DMVM_AVX512)
    dmvm_kernel_avx512(row, col, A, lda, x, y, adapative_repeat_count, output_file);
#elif defined(DMVM_AVX512_UNROLL2)
    dmvm_kernel_avx512_unroll2(row, col, A, lda, x, y, adapative_repeat_count, output_file);
#elif defined(DMVM_AVX512_UNROLL4)
    dmvm_kernel_avx512_unroll4(row, col, A, lda, x, y, adapative_repeat_count, output_file);
#elif defined(DMVM_DATA_TRAFFIC)
    dmvm_data_traffic(row, col, output_file);
#else
    fprintf(stderr, "no define test kernel !!!");
    fflush(stderr);
#endif

    data_release(A, x, y);
    return;
}

int main(){
    uint64_t col = env_get_uint64("COL", 10000);  // bytes >= 256
    uint64_t row_region_start = env_get_uint64("ROW_REGION_START" , 256);
    uint64_t row_region_end = env_get_uint64("ROW_REGION_END", 524288);
    uint64_t sample_points = env_get_uint64("SAMPLE_POINTS", 8);         
    uint64_t flops_per_test = env_get_uint64("FLOPS_PER_TEST", 5242880000); 

    const char* latency_output_filename_dir = env_get_string("LATENCY_OUTPUT_FILENAME_DIR", "./data/"); 
    const char* latency_output_filename_prefix = env_get_string("LATENCY_OUTPUT_FILENAME_PREFIX", "dmvm_mflops"); 
    const char* latency_output_filename_suffix = env_get_string("LATENCY_OUTPUT_FILENAME_SUFFIX", ".dat"); 

    printf("row_region_start : %ld\n", row_region_start);
    printf("row_region_end : %ld\n", row_region_end);
    printf("col : %ld\n", col);
    printf("sample_points : %ld\n", sample_points);
    printf("flops_per_test : %ld\n", flops_per_test);
    printf("latency_output_filename_DIR : %s\n", latency_output_filename_dir);
    printf("latency_output_filename_prefix : %s\n", latency_output_filename_prefix);
    printf("latency_output_filename_suffix : %s\n", latency_output_filename_suffix);
#ifdef DMVM_TILING
    printf("ROW_TILING : %ld\n", ROW_TILING);
#endif
    fflush(stdout);

    char filename[128];
    sprintf(filename, "%s%s%s", latency_output_filename_dir, latency_output_filename_prefix, latency_output_filename_suffix);
    FILE* output_file = fopen(filename, "w");

    for(uint64_t row_region_pow_2 = row_region_start; row_region_pow_2 < row_region_end; row_region_pow_2 *= 2){
        uint64_t step = row_region_pow_2 / sample_points;
        for(uint64_t row = row_region_pow_2; row < min(row_region_pow_2 * 2, row_region_end); row += step){
            dmvm_test(row, col, flops_per_test, output_file);
        }
    }
    dmvm_test(row_region_end, col, flops_per_test, output_file);

    fclose(output_file);
}