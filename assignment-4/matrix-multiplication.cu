#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <iostream>
#include <algorithm>
#include <vector>
#include <cmath>
#include <cstring>
#include <cuda_runtime.h>

using namespace std;

#define MAX_TILE_SIZE 32

// Part A: Naive Kernel (w/o shared memory)
__global__ void naiveKernel(float* A, float* B, float* C, int N)
{
    int row = blockIdx.y * blockDim.y + threadIdx.y;
    int col = blockIdx.x * blockDim.x + threadIdx.x;

    if(row < N && col < N)
    {
        float sum = 0.0f;

        for(int k = 0; k < N; k++)
        {
            sum += A[row * N + k] * B[k * N + col];
        }

        C[row * N + col] = sum;
    }
}

// Part B: Tiled Kernel (w/ shared memory)
__global__ void tiledKernel(float* A, float* B, float* C, int N, int TS)
{
    // I use a predefined MAX_TILE_SIZE so that my program will allocate shared memory for the max tile size, but only use the portion that it needs
    __shared__ float As[MAX_TILE_SIZE][MAX_TILE_SIZE];
    __shared__ float Bs[MAX_TILE_SIZE][MAX_TILE_SIZE];

    int row = blockIdx.y * TS + threadIdx.y;
    int col = blockIdx.x * TS + threadIdx.x;

    float sum = 0.0f;

    for(int tile = 0; tile < (N + TS - 1) / TS; tile++)
    {
        if(row < N && tile*TS + threadIdx.x < N)
            As[threadIdx.y][threadIdx.x] = A[row*N + tile*TS + threadIdx.x];
        else
            As[threadIdx.y][threadIdx.x] = 0.0f;

        if(col < N && tile*TS + threadIdx.y < N)
            Bs[threadIdx.y][threadIdx.x] = B[(tile*TS + threadIdx.y)*N + col];
        else
            Bs[threadIdx.y][threadIdx.x] = 0.0f;

        __syncthreads();

        for(int k = 0; k < TS; k++)
        {
            sum += As[threadIdx.y][k] * Bs[k][threadIdx.x];
        }

        __syncthreads();
    }

    if(row < N && col < N)
        C[row*N + col] = sum;
}

int main()
{
    for (int N = 8; N <= 32; N *= 2) 
    {
        for (int TS = 8; TS <= 32; TS *= 2) 
        {
            printf("\n%d by %d matrix with tile size of %d:\n", N, N, TS);

            size_t size = N * N * sizeof(float);

            float *A, *B, *C;
            float *dA, *dB, *dC;

            A = (float*)malloc(size);
            B = (float*)malloc(size);
            C = (float*)malloc(size);

            // Fill matrices A and B w/ random float numbers
            for (int i = 0; i < N * N; i++)
            {
                A[i] = rand() / (float)RAND_MAX;
                B[i] = rand() / (float)RAND_MAX;
            }

            cudaMalloc(&dA, size);
            cudaMalloc(&dB, size);
            cudaMalloc(&dC, size);

            cudaMemcpy(dA, A, size, cudaMemcpyHostToDevice);
            cudaMemcpy(dB, B, size, cudaMemcpyHostToDevice);

            dim3 block(TS, TS);
            dim3 grid((N + TS - 1)/TS, (N + TS - 1)/TS);

            cudaEvent_t start, stop;
            cudaEventCreate(&start);
            cudaEventCreate(&stop);

            // Time naive version
            cudaEventRecord(start);

            naiveKernel<<<grid, block>>>(dA, dB, dC, N);

            cudaEventRecord(stop);
            cudaEventSynchronize(stop);

            float naiveTime;
            cudaEventElapsedTime(&naiveTime, start, stop);
            printf("Naive time: %f\n", naiveTime);

            // Time tiled version
            cudaEventRecord(start);

            tiledKernel<<<grid, block>>>(dA, dB, dC, N, TS);

            cudaEventRecord(stop);
            cudaEventSynchronize(stop);

            float tiledTime;
            cudaEventElapsedTime(&tiledTime, start, stop);
            printf("Tiled time: %f\n", tiledTime);

            cudaMemcpy(C, dC, size, cudaMemcpyDeviceToHost);

            // Calculate the sqaured Forbenius Norm
            double frob = 0;
            for(int i = 0; i < N*N; i++)
            {
                frob += C[i] * C[i];
            }
                
            printf("Sqaured Frobenius Norm: %f\n", frob);

            // Free memory
            cudaFree(dA);
            cudaFree(dB);
            cudaFree(dC);

            free(A);
            free(B);
            free(C);
        }
    }

    return 0;
}