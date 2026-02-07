#define WIN32_LEAN_AND_MEAN
#include <stdio.h>
#include <mpi.h> // mpiexec
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

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

    int rank, nprocs;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &nprocs);

    BMPFileHeader fh;
    BMPInfoHeader fih;
    unsigned char* img = nullptr;

    int width = 0, height = 0, rowSize = 0;
    int totalBytes = 0;

    // Load BMP on rank 0
    if (rank == 0) {
        FILE* file = fopen("flowers.bmp", "rb");
        if (!file) {
            printf("Failed to open BMP\n");
            MPI_Abort(MPI_COMM_WORLD, 1);
        }

        fread(&fh.bfType, sizeof(short), 1, file);
        fread(&fh.bfSize, sizeof(int), 1, file);
        fread(&fh.bfReserved1, sizeof(short), 1, file);
        fread(&fh.bfReserved2, sizeof(short), 1, file);
        fread(&fh.bfOffBits, sizeof(int), 1, file);

        fread(&fih, sizeof(fih), 1, file);

        width  = fih.biWidth;
        height = fih.biHeight;
        rowSize = (width * 3 + 3) & ~3;
        printf("rowSize: %d", rowSize);
        totalBytes = rowSize * height;

        img = (unsigned char*)malloc(fih.biSizeImage);

        fseek(file, fh.bfOffBits, SEEK_SET);
        fread(img, 1, fih.biSizeImage, file);
        fclose(file);
    }

    // Broadcast metadata
    MPI_Bcast(&width, 1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(&height, 1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(&rowSize, 1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(&totalBytes, 1, MPI_INT, 0, MPI_COMM_WORLD);

    // Scatter image data
    int base = totalBytes / nprocs;
    int rem  = totalBytes % nprocs;

    int localSize = base + (rank < rem);
    

    int* counts = nullptr;
    int* displs = nullptr;

    if (rank == 0) {
        counts = (int*)malloc(nprocs * sizeof(int));
        displs = (int*)malloc(nprocs * sizeof(int));

        int offset = 0;
        for (int i = 0; i < nprocs; i++) {
            counts[i] = base + (i < rem);
            displs[i] = offset;
            offset += counts[i];
        }
    }

    unsigned char* local = (unsigned char*)malloc(localSize);

    MPI_Scatterv(
        img, counts, displs, MPI_UNSIGNED_CHAR,
        local, localSize, MPI_UNSIGNED_CHAR,
        0, MPI_COMM_WORLD
    );

    // Step 1: average illuminance
    float localSum = 0;
    int pixelCount = localSize;

    for (int i = 0; i < pixelCount; i += 3) {
        if ((i + 2) % rowSize == 0) {
            i += 2;
        }
        unsigned char B = local[i + 0];
        unsigned char G = local[i+ 1];
        unsigned char R = local[i + 2];
        localSum += (B + G + R) / 3;
    }

    float globalSum = 0;
    MPI_Reduce(&localSum, &globalSum, 1, MPI_FLOAT,
               MPI_SUM, 0, MPI_COMM_WORLD);

    double average = 0.0;
    if (rank == 0) {
        average = (double)(globalSum / (width * height));
        printf("Average illuminance: %f\n", average);
    }

    MPI_Bcast(&average, 1, MPI_DOUBLE, 0, MPI_COMM_WORLD);

    // Step 2: apply dot product
    for (int i = 0; i < pixelCount; i += 3) {
        if ((i + 2) % rowSize == 0) {
            i += 2;
        }
        local[i + 0] = (unsigned char)(local[i + 0] * 0.9 * average / 255.0);
        local[i + 1] = (unsigned char)(local[i + 1] * 0.8 * average / 255.0);
        local[i + 2] = (unsigned char)(local[i + 2] * 1.0 * average / 255.0);
    }

    // Gather back to rank 0
    MPI_Gatherv(
        local, localSize, MPI_UNSIGNED_CHAR,
        img, counts, displs, MPI_UNSIGNED_CHAR,
        0, MPI_COMM_WORLD
    );

    // Save output BMP
    if (rank == 0) {
        FILE* out = fopen("output.bmp", "wb");
        fwrite(&fh.bfType, sizeof(short), 1, out);
        fwrite(&fh.bfSize, sizeof(int), 1, out);
        fwrite(&fh.bfReserved1, sizeof(short), 1, out);
        fwrite(&fh.bfReserved2, sizeof(short), 1, out);
        fwrite(&fh.bfOffBits, sizeof(int), 1, out);

        fwrite(&fih, sizeof(fih), 1, out);

        fseek(out, fh.bfOffBits, SEEK_SET);
        fwrite(img, 1, fih.biSizeImage, out);
        fclose(out);

        free(counts);
        free(displs);
    }

    free(local);
    MPI_Finalize();
    return 0;
}