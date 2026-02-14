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

int main(int argc, char **argv)
{

    MPI_Init(&argc, &argv);

    int rank, size;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    int width = 0;
    int height = 0;
    unsigned char * image; 

    // Load BMP when rank == 0
    if (rank == 0)
    {
        FILE *file = fopen(argv[1], "rb");
        BMPFileHeader fh;
        BMPInfoHeader fih;

        // Load all data from the bitmap file into the struct instances
        fread(&fh.bfType, sizeof(short), 1, file);
        fread(&fh.bfSize, sizeof(int), 1, file);
        fread(&fh.bfReserved1, sizeof(short), 1, file);
        fread(&fh.bfReserved2, sizeof(short), 1, file);
        fread(&fh.bfOffBits, sizeof(int), 1, file);

        fread(&fih, sizeof(fih), 1, file);

        // Calculate image size
        width = fih.biWidth;
        height = fih.biHeight;

        printf("fih.biWidth = %u\n", width);
        printf("fih.biHeight = %u\n", height);

        image = (unsigned char *)malloc(fih.biSizeImage);

        fseek(file, fh.bfOffBits, SEEK_SET);
        fread(image, 1, fih.biSizeImage, file);
        fclose(file);
    }

    // Broadcast dimensions
    MPI_Bcast(&width, 1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(&height, 1, MPI_INT, 0, MPI_COMM_WORLD);

    // block decomposition
    vector<int> sendcounts(size);
    vector<int> displs(size);

    int rows_per_proc = height / size;
    int remainder = height % size;

    int offset = 0;
    for (int i = 0; i < size; i++)
    {
        int rows = rows_per_proc + (i < remainder ? 1 : 0);
        sendcounts[i] = rows * width;
        displs[i] = offset;
        offset += rows * width;
    }

    int local_rows = sendcounts[rank] / width;

    // Allocate local array WITH ghost rows
    vector<double> local_old((local_rows + 2) * width, 0.0);
    vector<double> local_new((local_rows + 2) * width, 0.0);

    // Scatter image
    vector<unsigned char> recvbuf(local_rows * width);

    MPI_Scatterv(
        image,
        sendcounts.data(),
        displs.data(),
        MPI_UNSIGNED_CHAR,
        recvbuf.data(),
        local_rows * width,
        MPI_UNSIGNED_CHAR,
        0,
        MPI_COMM_WORLD);

    // Copy into local_old starting at row 1 (skip ghost row 0)
    for (int i = 0; i < local_rows; i++)
        for (int j = 0; j < width; j++)
            local_old[(i + 1) * width + j] = recvbuf[i * width + j];

    // Halo exchange w/ MPI_Sendrecv

    int up = rank - 1;
    int down = rank + 1;

    // Exchange top ghost row
    if (up >= 0)
    {
        MPI_Sendrecv(
            &local_old[1 * width], // send first real row
            width,
            MPI_DOUBLE,
            up,
            0,
            &local_old[0 * width], // receive into ghost row
            width,
            MPI_DOUBLE,
            up,
            0,
            MPI_COMM_WORLD,
            MPI_STATUS_IGNORE);
    }

    // Exchange bottom ghost row
    if (down < size)
    {
        MPI_Sendrecv(
            &local_old[local_rows * width], // send last real row
            width,
            MPI_DOUBLE,
            down,
            0,
            &local_old[(local_rows + 1) * width], // receive ghost bottom
            width,
            MPI_DOUBLE,
            down,
            0,
            MPI_COMM_WORLD,
            MPI_STATUS_IGNORE);
    }

    // Do 1 stencil update
    for (int i = 1; i <= local_rows; i++)
    {
        for (int j = 1; j < width - 1; j++)
            local_new[i * width + j] = 0.25 * local_old[i * width + j] 
            + 0.1875 * (
                local_old[(i - 1) * width + j] 
                + local_old[(i + 1) * width + j] 
                + local_old[i * width + (j - 1)]
                + local_old[i * width + (j + 1)]
            );
    }

    // Compute the local checksum
    double local_sum = 0;

    for (int i = 1; i <= local_rows; i++)
        for (int j = 0; j < width; j++)
            local_sum += local_new[i * width + j];

    double global_sum = 0;

    // Get the global checksum to print
    MPI_Reduce(
        &local_sum,
        &global_sum,
        1,
        MPI_DOUBLE,
        MPI_SUM,
        0,
        MPI_COMM_WORLD);

    
    if (rank == 0)
        printf("Global checksum: %f", global_sum); 

    MPI_Finalize();
}
