#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <iostream>
#include <algorithm>
#include <mpi.h>
#include <vector>
#include <cmath>
#include <cstring>

using namespace std;
#pragma comment(lib, "Ws2_32.lib")

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

int main(int argc, char** argv) 
{
    MPI_Init(&argc, &argv); 

    int rank, size; 
    MPI_Comm_rank(MPI_COMM_WORLD, &rank); 
    MPI_Comm_size(MPI_COMM_WORLD, &size); 
 
    int key = atoi(argv[3]); // Key is the number that every byte in the bmp will be multiplied by

    unsigned int* entireFile; // int now b/c everything is stored as int in the xyz file
    BMPFileHeader fh;
    BMPInfoHeader fih;
    int totalValues = 0;

    double startTime;

    // Load xyz file only on rank 0
    if (rank == 0) 
    {
        startTime = MPI_Wtime(); 
        FILE* file = fopen(argv[1], "rb"); 

        fseek(file, 0, SEEK_END); 
        long fileSize = ftell(file); 
        rewind(file); 

        totalValues = fileSize / sizeof(unsigned int); 

        entireFile = (unsigned int*)malloc(totalValues * sizeof(unsigned int)); 

        fread(entireFile, sizeof(unsigned int), totalValues,  file); 
        fclose(file);
    }

    MPI_Bcast(&totalValues, 1, MPI_INT, 0, MPI_COMM_WORLD);

    vector<int> sendcounts(size); // List of how many bytes each process receives
    vector<int> displs(size); // List of the byte # where each process should start at

    int base = totalValues / size;
    int remainder = totalValues % size;

    for (int i = 0; i < size; i++) 
    {
        sendcounts[i] = base; 
        if (i < remainder) 
            sendcounts[i]++; 
    }

    displs[0] = 0;  
    for (int i = 1; i < size; i++) 
        displs[i] = displs[i - 1] + sendcounts[i - 1]; 

    int localSize = sendcounts[rank]; 

    unsigned int* localEncoded = (unsigned int*)malloc(localSize * sizeof(unsigned int)); 
    unsigned char* decodedBytes = (unsigned char*)malloc(localSize * sizeof(unsigned char)); 

    MPI_Scatterv(
        entireFile,
        sendcounts.data(),
        displs.data(),
        MPI_UNSIGNED,
        localEncoded,
        localSize,
        MPI_UNSIGNED,
        0,
        MPI_COMM_WORLD
    );

    unsigned int localChecksum = 0; 

    // Divide every byte by the key
    for (int i = 0; i < localSize; i++) 
    {
        decodedBytes[i] = (unsigned char)(localEncoded[i] / key); 
        localChecksum += decodedBytes[i]; 
    }

    unsigned char* gatheredFileData;

    if (rank == 0)
        gatheredFileData = (unsigned char*)malloc(totalValues * sizeof(unsigned char)); 

    MPI_Gatherv(
        decodedBytes, 
        localSize, 
        MPI_UNSIGNED_CHAR, 
        gatheredFileData, 
        sendcounts.data(), 
        displs.data(), 
        MPI_UNSIGNED_CHAR, 
        0, 
        MPI_COMM_WORLD
    );

    unsigned int globalChecksum = 0; 

    MPI_Reduce(
        &localChecksum, 
        &globalChecksum, 
        1, 
        MPI_UNSIGNED, 
        MPI_SUM, 
        0, 
        MPI_COMM_WORLD
    ); 

    if (rank == 0) 
    {
        FILE* out = fopen(argv[2], "wb"); 

        fwrite(gatheredFileData, sizeof(unsigned char), totalValues, out); 
        fclose(out); 

        double endTime = MPI_Wtime(); 

        printf("Decoding Finished: \n");
        printf("Checksum: %u\n", globalChecksum);
        printf("Time: %f \n", endTime - startTime);
    }

    MPI_Finalize();
}
