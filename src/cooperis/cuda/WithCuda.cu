#include <driver_types.h>
#include <gsl/gsl_matrix.h>
#include <cuda_runtime.h>
#include <iostream>
#include "WithCuda.h"

#define CUDA_BLOCK_SIZE 1024

namespace withcuda {

void cuda_assert(cudaError_t code, const char* file = __FILE__, int line = __LINE__)
{
    if (code != cudaSuccess) {
        std::cerr << "CUDA error: " << cudaGetErrorString(code) << " at " << file << ":" << line << std::endl;
        exit(code);
    }
}

__global__ void matrix_n_m_compute_kernel(const double* k_du_sin_cos, const double* k_du_sin_sin, double neg_n, double neg_m, double alpha, double PHI, double* dest_real, double* dest_img, size_t size)
{
    unsigned int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i < size) {
        double tmp_img = -((neg_n * k_du_sin_cos[i]) + (neg_m * k_du_sin_sin[i]) + alpha + PHI);
        dest_real[i] += cos(tmp_img);
        dest_img[i] += sin(tmp_img);
    }
}

void matrix_n_m_compute(const cuda_matrix& k_du_sin_cos, const cuda_matrix& k_du_sin_sin, double neg_n, double neg_m, double alpha, double PHI, cuda_cmatrix& dest)
{
    int num_blocks = (k_du_sin_cos.rows * k_du_sin_cos.cols + CUDA_BLOCK_SIZE - 1) / CUDA_BLOCK_SIZE;
    matrix_n_m_compute_kernel<<<num_blocks, CUDA_BLOCK_SIZE>>>(k_du_sin_cos.data, k_du_sin_sin.data, neg_n, neg_m, alpha, PHI, dest.data_real, dest.data_img, dest.rows * dest.cols);
    cuda_assert(cudaDeviceSynchronize(), __FILE__, __LINE__);
}

void gsl_matrix_to_cuda_matrix(cuda_matrix& cuda, const gsl_matrix* gsl)
{
    cuda.rows = gsl->size1;
    cuda.cols = gsl->size2;
    cuda_assert(cudaMalloc(&cuda.data, cuda.rows * cuda.cols * sizeof(double)), __FILE__, __LINE__);
    cuda_assert(cudaMemcpy(cuda.data, gsl->data, cuda.rows * cuda.cols * sizeof(double), cudaMemcpyHostToDevice), __FILE__, __LINE__);
}

void cuda_matrix_to_gsl_matrix(gsl_matrix* gsl, const cuda_matrix& cuda)
{
    cuda_assert(cudaMemcpy(gsl->data, cuda.data, cuda.rows * cuda.cols * sizeof(double), cudaMemcpyDeviceToHost), __FILE__, __LINE__);
}

void cuda_cmatrix_to_gsl_cmatrix(gsl_matrix_complex* gsl, const cuda_cmatrix& cuda)
{
    double* data_real = (double*) malloc(cuda.rows * cuda.cols * sizeof(double));
    double* data_img = (double*) malloc(cuda.rows * cuda.cols * sizeof(double));
    cuda_assert(cudaMemcpy(data_real, cuda.data_real, cuda.rows * cuda.cols * sizeof(double), cudaMemcpyDeviceToHost), __FILE__, __LINE__);
    cuda_assert(cudaMemcpy(data_img, cuda.data_img, cuda.rows * cuda.cols * sizeof(double), cudaMemcpyDeviceToHost), __FILE__, __LINE__);
    for (unsigned int i = 0; i < cuda.rows; i++)
        for (unsigned int j = 0; j < cuda.cols; j++)
            gsl_matrix_complex_set(gsl, i, j, {data_real[i * cuda.cols + j], data_img[i * cuda.cols + j]});
}

void cuda_matrix_alloc(cuda_matrix& cuda, unsigned int rows, unsigned int cols)
{
    cuda.rows = rows;
    cuda.cols = cols;
    cuda_assert(cudaMalloc(&cuda.data, rows * cols * sizeof(double)), __FILE__, __LINE__);
    cuda_assert(cudaMemset(cuda.data, 0, rows * cols * sizeof(double)), __FILE__, __LINE__);
}

void cuda_cmatrix_alloc(cuda_cmatrix& cuda, unsigned int rows, unsigned int cols)
{
    cuda.rows = rows;
    cuda.cols = cols;
    cuda_assert(cudaMalloc(&cuda.data_real, rows * cols * sizeof(double)), __FILE__, __LINE__);
    cuda_assert(cudaMalloc(&cuda.data_img, rows * cols * sizeof(double)), __FILE__, __LINE__);
    cuda_assert(cudaMemset(cuda.data_real, 0, rows * cols * sizeof(double)), __FILE__, __LINE__);
    cuda_assert(cudaMemset(cuda.data_img, 0, rows * cols * sizeof(double)), __FILE__, __LINE__);
}

void cuda_matrix_free(cuda_matrix& cuda)
{
    cuda_assert(cudaFree(cuda.data), __FILE__, __LINE__);
    cuda.rows = 0;
    cuda.cols = 0;
    cuda.data = nullptr;
}

void cuda_cmatrix_free(cuda_cmatrix& cuda)
{
    cuda_assert(cudaFree(cuda.data_real), __FILE__, __LINE__);
    cuda_assert(cudaFree(cuda.data_img), __FILE__, __LINE__);
    cuda.rows = 0;
    cuda.cols = 0;
    cuda.data_real = nullptr;
    cuda.data_img = nullptr;
}
} // namespace withcuda
