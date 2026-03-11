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

    int key = atoi(argv[3]);

    unsigned char* entireFile;
    BMPFileHeader fh;
    BMPInfoHeader fih;
    int totalBytes = 0; 

    double startTime;

    // Load BMP only on rank 0
    if (rank == 0) 
    {
        startTime = MPI_Wtime(); 
        FILE* file = fopen(argv[1], "rb");

        // Read the total bytes of the file from the file header, then reset the file pointer w/ rewind(file)
        fread(&fh.bfType, sizeof(short), 1, file); 
        fread(&fh.bfSize, sizeof(int), 1, file); 
        totalBytes = fh.bfSize; 
        rewind(file); 

        entireFile = (unsigned char*)malloc(totalBytes); 

        fread(entireFile, sizeof(unsigned char), totalBytes, file); 
        fclose(file); 
    } 

    MPI_Bcast(&totalBytes, 1, MPI_INT, 0, MPI_COMM_WORLD);

    vector<int> sendcounts(size); // List of how many bytes each process receives
    vector<int> displs(size); // List of the byte # where each process should start at

    int base = totalBytes / size;
    int remainder = totalBytes % size;

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

    unsigned char* localBytes = (unsigned char*)malloc(localSize);
    unsigned int* encoded = (unsigned int*)malloc(localSize * sizeof(unsigned int));

    MPI_Scatterv(
        entireFile, 
        sendcounts.data(), 
        displs.data(), 
        MPI_UNSIGNED_CHAR, 
        localBytes, 
        localSize, 
        MPI_UNSIGNED_CHAR, 
        0, 
        MPI_COMM_WORLD 
    );

    unsigned int localChecksum = 0;

    // Multiply every byte by the key
    for (int i = 0; i < localSize; i++)
    {
        encoded[i] = (unsigned int)localBytes[i] * key;
        localChecksum += encoded[i];
    }

    unsigned int* gatheredFileData;

    if (rank == 0)
        gatheredFileData = (unsigned int*)malloc(totalBytes * sizeof(unsigned int)); 

    MPI_Gatherv(
        encoded, 
        localSize, 
        MPI_UNSIGNED, 
        gatheredFileData, 
        sendcounts.data(), 
        displs.data(), 
        MPI_UNSIGNED, 
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

    // Write BMP only on rank 0
    if (rank == 0)
    {
        FILE* out = fopen(argv[2], "wb");

        fwrite(gatheredFileData, sizeof(unsigned int), totalBytes, out); 
        fclose(out); 

        double endTime = MPI_Wtime(); 

        printf("Encoding Finished:\n"); 
        printf("Checksum: %u\n", globalChecksum); 
        printf("Time taken: %f \n", endTime - startTime); 
    }

    MPI_Finalize();
}
