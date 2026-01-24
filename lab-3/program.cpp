#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <atomic>
#include <math.h>
#include <string.h>

#pragma comment(lib, "Ws2_32.lib")

#define THREADS 4

// Global Variables
unsigned char *g_data;
int g_width;
int g_height;
int g_rowSize;

double g_partialSums[THREADS];
double g_Lavg;

int g_gatherValues[THREADS] = {0};

std::atomic<int> senseCount(0);
std::atomic<int> senseGlobal(0);

struct BMPFileHeader
{
    unsigned short bfType;
    unsigned int bfSize;
    unsigned short bfReserved1;
    unsigned short bfReserved2;
    unsigned int bfOffBits;
};

struct BMPInfoHeader
{
    unsigned int biSize;
    int biWidth;
    int biHeight;
    unsigned short biPlanes;
    unsigned short biBitCount;
    unsigned int biCompression;
    unsigned int biSizeImage;
    int biXPelsPerMeter;
    int biYPelsPerMeter;
    unsigned int biClrUsed;
    unsigned int biClrImportant;
};

struct ThreadArgs
{
    int id;
    int yStart;
    int yEnd;
    bool useDIY;
};

// Sense-Reversing Barrier
void sr_barrier(int &localSense)
{
    localSense = !localSense;

    if (senseCount.fetch_add(1) == THREADS - 1)
    {
        senseCount.store(0);
        senseGlobal.store(localSense, std::memory_order_release);
    }
    else
    {
        while (senseGlobal.load(std::memory_order_acquire) != localSense)
        {
            Sleep(0);
        }
    }
}

// DIY Gather
void gather(int gather_id, int *gv, int id, int tc)
{
    gv[id] = gv[id] + 1;
    while (1)
    {
        int breakout = 1;
        for (int i = 0; i < tc; i++)
            if (gv[i] < gather_id)
                breakout = 0;

        if (breakout == 1)
            break;
    }
}

DWORD WINAPI worker(LPVOID param)
{
    ThreadArgs *args = (ThreadArgs *)param;
    int id = args->id;
    int localSense = 0;

    // Stage 1
    double sumLogL = 0.0;

    for (int y = args->yStart; y < args->yEnd; y++)
    {
        unsigned char *row = g_data + y * g_rowSize;

        for (int x = 0; x < g_width; x++)
        {
            float R = (float)(row[x*3+2]/255.0f);
            float G = (float)(row[x*3+1]/255.0f);
            float B = (float)(row[x*3+0]/255.0f);

            float L = 0.2126 * R + 0.7152 * G + 0.0722 * B;
            sumLogL += log(L + 1.0);
        }
    }

    g_partialSums[id] = sumLogL;

    // Compute g_Lavg
    if (id == 0)
    {
        float S = 0.0;
        for (int i = 0; i < THREADS; i++)
            S += g_partialSums[i];

        int totalPixels = g_width * g_height;
        g_Lavg = exp(S / totalPixels) - 1.0;
        printf("DEBUG: g_Lavg = %f\n", g_Lavg);
    }
    
    // 1st Gather - Use the gather function that was requested, either diy or nodiy
    if (args->useDIY)
        gather(2, g_gatherValues, id, THREADS);
    else
        sr_barrier(localSense);

    // Stage 2
    const float a = 0.18f;
    for (int y = args->yStart; y < args->yEnd; y++)
    {
        unsigned char *row = g_data + y * g_rowSize;

        for (int x = 0; x < g_width; x++)
        {
            unsigned char *px = &row[x * 3];

            float R = (float)(row[x*3+2]/255.0f);
            float G = (float)(row[x*3+1]/255.0f);
            float B = (float)(row[x*3+0]/255.0f);

            float L = 0.2126 * R + 0.7152 * G + 0.0722 * B;
            float scale = 0.0;

            if (L > 0.0)
            {
                float Lm = (a / g_Lavg) * L;
                float Ld = Lm / (1 + Lm);
                scale = Ld / L;
            }

            px[0] = (unsigned char)fmin(255.0, B * scale * 255.0);
            px[1] = (unsigned char)fmin(255.0, G * scale * 255.0);
            px[2] = (unsigned char)fmin(255.0, R * scale * 255.0);
            if (id == 0 && y == args->yStart && x == 0) {
                printf("DEBUG: R=%f G=%f B=%f L=%f scale=%f\n", R, G, B, L, scale);
            }
        }
    }

    // 2nd Gather - Use the gather function that was requested, either diy or nodiy
    if (args->useDIY)
        gather(3, g_gatherValues, id, THREADS);
    else
        sr_barrier(localSense);

    return 0;
}

int main(int argc, char **argv)
{
    // If the 3rd argument is "diy" then useDIY is true
    bool useDIY = strcmp(argv[3], "diy") == 0;
    // Open given bitmap file
    // The variable file is now a pointer to the open bitmap file
    FILE *file = fopen(argv[1], "rb");
    BMPFileHeader fh;
    BMPInfoHeader fih;

    // Load all the data from the bitmap file into the struct instances
    fread(&fh.bfType, sizeof(short), 1, file);
    fread(&fh.bfSize, sizeof(int), 1, file);
    fread(&fh.bfReserved1, sizeof(short), 1, file);
    fread(&fh.bfReserved2, sizeof(short), 1, file);
    fread(&fh.bfOffBits, sizeof(int), 1, file);

    fread(&fih, sizeof(fih), 1, file);

    // Calculate row size and image size
    g_width = fih.biWidth;
    g_height = fih.biHeight;
    g_rowSize = ((g_width * 3 + 3) & ~3);

    printf("fih.biHeight = %u\n", g_height);
    printf("g_rowSize = %d\n", g_rowSize);

    g_data = (unsigned char *)malloc(fih.biSizeImage);

    fseek(file, fh.bfOffBits, SEEK_SET);
    fread(g_data, 1, fih.biSizeImage, file);
    fclose(file);


    // Spawn threads
    HANDLE threads[THREADS];
    ThreadArgs args[THREADS];

    int rowsPerThread = g_height / THREADS;

    for (int i = 0; i < THREADS; i++)
    {
        args[i].id = i;
        args[i].yStart = i * rowsPerThread;
        args[i].yEnd = (i == THREADS - 1) ? g_height : (i + 1) * rowsPerThread;
        args[i].useDIY = useDIY;

        threads[i] = CreateThread(NULL, 0, worker, &args[i], 0, NULL);
    }

    WaitForMultipleObjects(THREADS, threads, TRUE, INFINITE);

    // Write output
    FILE *out = fopen(argv[2], "wb");
    fwrite(&fh.bfType, sizeof(short), 1, out);
    fwrite(&fh.bfSize, sizeof(int), 1, out);
    fwrite(&fh.bfReserved1, sizeof(short), 1, out);
    fwrite(&fh.bfReserved2, sizeof(short), 1, out);
    fwrite(&fh.bfOffBits, sizeof(int), 1, out);

    fwrite(&fih, sizeof(fih), 1, out);

    fseek(out, fh.bfOffBits, SEEK_SET);
    fwrite(g_data, 1, fih.biSizeImage, out);
    fclose(out);

    free(g_data);

    printf( "Finished using %s barrier\n", useDIY ? "DIY gather" : "sense-reversing");
}
