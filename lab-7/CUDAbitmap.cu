#include <iostream>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <atomic>
#include <math.h>
#include <string.h>
#include <cuda_runtime.h>

struct BMPFileHeader
{
    unsigned short bfType;      // 'BM' = 0x4D42
    unsigned int bfSize;        // file size in bytes
    unsigned short bfReserved1; // must be 0
    unsigned short bfReserved2; // must be 0
    unsigned int bfOffBits;     // offset to pixel data
};

struct BMPInfoHeader
{
    unsigned int biSize;         // header size (40)
    int biWidth;                 // image width
    int biHeight;                // image height
    unsigned short biPlanes;     // must be 1
    unsigned short biBitCount;   // 24 for RGB
    unsigned int biCompression;  // 0 = BI_RGB
    unsigned int biSizeImage;    // image data size (can be 0 for BI_RGB)
    int biXPelsPerMeter;         // resolution
    int biYPelsPerMeter;         // resolution
    unsigned int biClrUsed;      // colors used (0)
    unsigned int biClrImportant; // important colors (0)
};

static void checkCuda(cudaError_t e, const char* msg)
    {
    if (e != cudaSuccess)
        {
        std::cerr << msg << ": " << cudaGetErrorString(e) << "\n";
        std::exit(1);
        }
    }

// __global__ void addKernel(const data, int n)
//     {
//     int i = blockIdx.x * blockDim.x + threadIdx.x; //intrinsic var
//     if (i < n) 
//         c[i] = a[i] + b[i]; 
//     }

__global__ void swap(const unsigned char * data, int n)
    {
    int i = blockIdx.x * blockDim.x + threadIdx.x; //intrinsic var
    if (i < n) {
        int pixel_index = i * 3;
        int temp = data[pixel_index]; // temp = red
        data[pixel_index] = data[pixel_index + 2]; // red = blue
        data[pixel_index + 2] = temp; // blue = temp
    }
    }

int main()
    {
    const int N = 1 << 20; // ~1 million
    const size_t bytes = N * sizeof(float);

    // float* h_a = new float[N];
    // float* h_b = new float[N];
    // float* h_c = new float[N];

    unsigned char * d_data = new unsigned char[N];

     // The variable file is now a pointer to the open bitmap file
    FILE *file = fopen("test.bmp", "rb");
    BMPFileHeader fh;
    BMPInfoHeader fih;

    // Load all the data from the bitmap file into the struct instances
    fread(&fh.bfType, sizeof(short), 1, file);
    fread(&fh.bfSize, sizeof(int), 1, file);
    fread(&fh.bfReserved1, sizeof(short), 1, file);
    fread(&fh.bfReserved2, sizeof(short), 1, file);
    fread(&fh.bfOffBits, sizeof(int), 1, file);

    fread(&fih, sizeof(fih), 1, file);

    unsigned char *data = (unsigned char *)malloc(fih.biSizeImage);

    fseek(file, fh.bfOffBits, SEEK_SET);
    fread(data, 1, fih.biSizeImage, file);
    fclose(file);

    // for (int i = 0; i < N; i++)
    //     {
    //     h_a[i] = (float)i;
    //     h_b[i] = 2.0f * (float)i;
    //     }

    // float* d_a = nullptr, * d_b = nullptr, * d_c = nullptr;
    // cudaMalloc(&d_a, bytes);
    // cudaMalloc(&d_b, bytes);
    checkCuda(cudaMalloc(&d_data, bytes), "cudaMalloc data");

    checkCuda(cudaMemcpy(d_data, data, bytes, cudaMemcpyHostToDevice), "Memcpy H->D a");//copy data CPU to GPU
    // checkCuda(cudaMemcpy(d_b, h_b, bytes, cudaMemcpyHostToDevice), "Memcpy H->D b");

    int threads = 256;
    int blocks = (N + threads - 1) / threads;

    swap <<< blocks, threads >>> (d_data, N);

    checkCuda(cudaGetLastError(), "Kernel launch");
    checkCuda(cudaDeviceSynchronize(), "Kernel sync"); //kinda barrier fct: CPU must wait till GPU is done

    checkCuda(cudaMemcpy(data, d_data, bytes, cudaMemcpyDeviceToHost), "Memcpy D->H c");//copy data GPU to CPU

    // quick verification
    // bool ok = true;
    // for (int i = 0; i < 10; i++)
    //     std::cout << h_c[i] << " ";
    // std::cout << "\n";

    // for (int i = 0; i < N; i++)
    //     {
    //     float expected = h_a[i] + h_b[i];
    //     if (h_c[i] != expected) { ok = false; break; }
    //     }

    // std::cout << (ok ? "PASS\n" : "FAIL\n");

    // cudaFree(d_a);
    // cudaFree(d_b);
    // cudaFree(d_c);

    // delete[] h_a;
    // delete[] h_b;
    // delete[] h_c;

    // return ok ? 0 : 1;
    FILE *out = fopen("test.bmp", "wb");
    fwrite(&fh.bfType, sizeof(short), 1, out);
    fwrite(&fh.bfSize, sizeof(int), 1, out);
    fwrite(&fh.bfReserved1, sizeof(short), 1, out);
    fwrite(&fh.bfReserved2, sizeof(short), 1, out);
    fwrite(&fh.bfOffBits, sizeof(int), 1, out);

    fwrite(&fih, sizeof(fih), 1, out);

    fseek(out, fh.bfOffBits, SEEK_SET);
    fwrite(data, 1, fih.biSizeImage, out);
    fclose(out);

    free(data);


    }
