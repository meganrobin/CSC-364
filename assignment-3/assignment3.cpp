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
    unsigned char * image = nullptr;
    BMPFileHeader fh;
    BMPInfoHeader fih;

    // Load BMP when on rank 0
    if (rank == 0)
    {
        FILE *file = fopen("coolingribs.bmp", "rb");

        // Load all data from the bitmap file into the struct instances
        fread(&fh.bfType, sizeof(short), 1, file);
        fread(&fh.bfSize, sizeof(int), 1, file);
        fread(&fh.bfReserved1, sizeof(short), 1, file);
        fread(&fh.bfReserved2, sizeof(short), 1, file);
        fread(&fh.bfOffBits, sizeof(int), 1, file);

        fread(&fih, sizeof(fih), 1, file);

        width = fih.biWidth;
        height = fih.biHeight;

        printf("fih.biWidth = %u\n", width);
        printf("fih.biHeight = %u\n", height);

        image = (unsigned char *)malloc(fih.biSizeImage);

        fseek(file, fh.bfOffBits, SEEK_SET);
        fread(image, 1, fih.biSizeImage, file);
        fclose(file);
    }

    // Broadcast width and height of the image to all processes
    MPI_Bcast(&width, 1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(&height, 1, MPI_INT, 0, MPI_COMM_WORLD);

    // Block decomposition
    vector<int> sendcounts(size);
    vector<int> displs(size);

    int rows_per_proc = height / size;
    int remainder = height % size;

    int row_size = (width * 3 + 3) & ~3;

    int offset = 0;
    for (int i = 0; i < size; i++)
    {
        int rows = rows_per_proc + (i < remainder ? 1 : 0);
        sendcounts[i] = rows * row_size;
        displs[i] = offset;
        offset += rows * row_size;
    }

    int local_rows = sendcounts[rank] / row_size;
    printf("local_rows = %d\n", local_rows);

    // Allocate local arrays of: just red values w/ ghost rows on top and bottom
    vector<double> local_old((local_rows + 2) * width);
    vector<double> local_new((local_rows + 2) * width);

    // Scatter image
    vector<unsigned char> recvbuf(local_rows * row_size);

    MPI_Scatterv(
        image,
        sendcounts.data(),
        displs.data(),
        MPI_UNSIGNED_CHAR,
        recvbuf.data(),
        local_rows * row_size,
        MPI_UNSIGNED_CHAR,
        0,
        MPI_COMM_WORLD);

    // Copy into local_old starting at row 1 (skip ghost row 0) - JUST RED VALUES
    
    for (int i = 0; i < local_rows; i++) {
        for (int j = 0; j < width; j++) {
            int pixel_index = i * row_size + j * 3 + 2; // only red value
            local_old[(i + 1) * width + j] = recvbuf[pixel_index];
        }
    }

    double threshold = 1;
    double global_max_dT = threshold + 0.01;
    int iter = 0;
    double a = 0.2; 
    
    // Halo exchange stuff
    int up = rank - 1;
    int down = rank + 1;

    while (global_max_dT > threshold)
    {
        iter++;

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

        double local_max_dT = 0.0;

        for (int y = 1; y <= local_rows; y++)
        {
            for (int x = 1; x < width - 1; x++)
            {
                double curr_pixel = local_old[y * width + x];
                double north = local_old[(y - 1) * width + x];
                double south = local_old[(y + 1) * width + x];
                double west = local_old[y * width + (x - 1)];
                double east = local_old[y * width + (x + 1)];
                
                local_new[y * width + x] = curr_pixel + a * ((north + south + west + east) - 4 * curr_pixel);

                double dT = fabs(local_new[y * width + x] - curr_pixel);
                
                local_max_dT = max(dT, local_max_dT);
            }
        }

        // Do All reduce globally
        MPI_Allreduce(
            &local_max_dT, // Address of thing to send
            &global_max_dT, // Address of recieve buffer
            1,
            MPI_DOUBLE, // Datatype of elements of send buffer
            MPI_MAX, // Operation to do on all - get the max dT
            MPI_COMM_WORLD
        );
        printf("global_max_dT: %f", global_max_dT);

        // Do the buffer swap
        local_old.swap(local_new);
    }
    
    if(rank == 0)
        printf("Iterations: %d\n", iter);

    // Copy back into buffer
    for (int i = 0; i < local_rows; i++)
        for (int j = 0; j < width; j++)
            recvbuf[i * row_size + j * 3 + 2] = (unsigned char)min(255.0, max(0.0, local_old[(i + 1) * width + j]));

    MPI_Gatherv(
        recvbuf.data(),
        local_rows * row_size,
        MPI_UNSIGNED_CHAR,
        image,
        sendcounts.data(),
        displs.data(),
        MPI_UNSIGNED_CHAR,
        0,
        MPI_COMM_WORLD
    );

    // Write BMP only on rank 0
    if (rank == 0)
    {
        FILE *out = fopen("test.bmp", "wb");

        fwrite(&fh.bfType, sizeof(short), 1, out);
        fwrite(&fh.bfSize, sizeof(int), 1, out);
        fwrite(&fh.bfReserved1, sizeof(short), 1, out);
        fwrite(&fh.bfReserved2, sizeof(short), 1, out);
        fwrite(&fh.bfOffBits, sizeof(int), 1, out);

        fwrite(&fih, sizeof(fih), 1, out);

        fseek(out, fh.bfOffBits, SEEK_SET);
        fwrite(image, 1, fih.biSizeImage, out);
        fclose(out);

    }

    MPI_Finalize();
}
