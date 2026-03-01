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

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION

#include "stb_image.h"
#include "stb_image_write.h"

#define BLOCK_SIZE 16


__constant__ int Mx[3][3] = {
    {-1, 0, 1},
    {-2, 0, 2},
    {-1, 0, 1}
};

__constant__ int My[3][3] = {
    {-1, -2, -1},
    {0,  0,  0},
    {1,  2,  1}
};

__global__ void sobelKernel(unsigned char* input, unsigned char* output, int width, int height, float* blockSum)
{
    __shared__ unsigned char tile[BLOCK_SIZE + 2][BLOCK_SIZE + 2];
    __shared__ float localSum[BLOCK_SIZE * BLOCK_SIZE];

    int tx = threadIdx.x;
    int ty = threadIdx.y;

    int x = blockIdx.x * BLOCK_SIZE + tx;
    int y = blockIdx.y * BLOCK_SIZE + ty;

    int sharedX = tx + 1;
    int sharedY = ty + 1;

    // Load the center pixel
    if (x < width && y < height)
        tile[sharedY][sharedX] = input[y * width + x];
    else
        tile[sharedY][sharedX] = 0;

    // Load halo borders
    if (tx == 0 && x > 0) 
    {
        tile[sharedY][0] = input[y * width + (x - 1)];
    }
    if (tx == BLOCK_SIZE - 1 && x < width - 1)
    {    
        tile[sharedY][BLOCK_SIZE + 1] = input[y * width + (x + 1)];
    }
    if (ty == 0 && y > 0)
    {
        tile[0][sharedX] = input[(y - 1) * width + x];
    }
    if (ty == BLOCK_SIZE - 1 && y < height - 1)
    {
        tile[BLOCK_SIZE + 1][sharedX] = input[(y + 1) * width + x];
    }

    __syncthreads();

    float Gx = 0;
    float Gy = 0;

    if (x > 0 && x < width - 1 && y > 0 && y < height - 1)
    {
        for (int i = -1; i <= 1; i++)
        {
            for (int j = -1; j <= 1; j++)
            {
                unsigned char pixel = tile[sharedY + i][sharedX + j];
                Gx += pixel * Mx[i + 1][j + 1];
                Gy += pixel * My[i + 1][j + 1];
            }
        }

        float edge = sqrtf(Gx * Gx + Gy * Gy);
        if (edge > 255) edge = 255;

        output[y * width + x] = (unsigned char)edge;
        localSum[ty * BLOCK_SIZE + tx] = edge;
    }
    else
    {
        localSum[ty * BLOCK_SIZE + tx] = 0;
    }

    __syncthreads();

    int tid = ty * BLOCK_SIZE + tx;

    for (int difference = (BLOCK_SIZE * BLOCK_SIZE) / 2; difference > 0; difference >>= 1)
    {
        if (tid < difference)
            localSum[tid] += localSum[tid + difference];

        __syncthreads();
    }

    if (tid == 0)
    {
        blockSum[blockIdx.y * gridDim.x + blockIdx.x] = localSum[0];
    }
}

int main()
{
    int width;
    int height;
    int channels;

    unsigned char* img = stbi_load("test.bmp", &width, &height, &channels, 1);
    if (!img)
    {
        printf("Could not load image");
        return -1;
    }

    size_t imgSize = width * height * sizeof(unsigned char);

    unsigned char* d_input;
    unsigned char* d_output;
    float* d_blockSum;

    int gridX = (width + BLOCK_SIZE - 1) / BLOCK_SIZE;
    int gridY = (height + BLOCK_SIZE - 1) / BLOCK_SIZE;

    cudaMalloc(&d_input, imgSize);
    cudaMalloc(&d_output, imgSize);
    cudaMalloc(&d_blockSum, gridX * gridY * sizeof(float));

    cudaMemcpy(d_input, img, imgSize, cudaMemcpyHostToDevice);

    dim3 block(BLOCK_SIZE, BLOCK_SIZE);
    dim3 grid(gridX, gridY);

    sobelKernel<<<grid, block>>>(d_input, d_output, width, height, d_blockSum);

    vector<unsigned char> output(width * height);
    cudaMemcpy(output.data(), d_output, imgSize, cudaMemcpyDeviceToHost);

    stbi_write_bmp("result.bmp", width, height, 1, output.data());

    cudaFree(d_input);
    cudaFree(d_output);
    cudaFree(d_blockSum);
    stbi_image_free(img);

    return 0;
}
