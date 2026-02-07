#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdio.h>
#include <windows.h>
#include <stdint.h>
#include <stdlib.h>
#include <iostream>
#include <fstream>
#include <thread>
using namespace std;
#include <atomic>
#include <math.h>
#include <string.h>
#include <tchar.h>
#include <vector>

#pragma comment(lib, "Ws2_32.lib")

#define PORT "8081"

// Mutex
typedef struct
{
    atomic_flag f;
} lock_t;

static void init(lock_t* m)
{
    atomic_flag_clear(&m->f); // f = 0
}

static void lock(lock_t* m) // t1 t2
{
    while (atomic_flag_test_and_set(&m->f)) { /* spin */ }
}

static void unlock(lock_t* m)
{
    atomic_flag_clear(&m->f);
}

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

// Global Variables
unsigned char *g_data;
int g_width;
int g_height;
int g_rowSize;
lock_t row_lock;
int next_row;

// Helper funct - Recieve all bytes
int recv_all(SOCKET s, char* buf, int len)
{
    int recvd = 0;
    while (recvd < len)
    {
        int n = recv(s, buf + recvd, len - recvd, 0);
        if (n <= 0) return -1;
        recvd += n;
    }
    return recvd;
}

// Helper function to contiuously send rows to the next available client (pass type is 0 for horizontal or 1 for vertical)
void send_rows(vector<SOCKET>& clients, int pass_type) {
    next_row = 0;

    while (true) {
        bool done = true;

        for (SOCKET c : clients) {
            int row;

            lock(&row_lock);
            if (next_row >= g_height) row = -1;
            else row = next_row++;
            unlock(&row_lock);

            send(c, (char*)&row, sizeof(int), 0);
            if (row == -1) continue;

            done = false;

            send(c, (char*)&pass_type, sizeof(int), 0);
            send(c, (char*)&g_width, sizeof(int), 0);
            send(c, (char*)&g_height, sizeof(int), 0);
            send(c, (char*)g_data, g_height * g_rowSize, 0);

            recv_all(c, (char*)&g_data[row * g_rowSize], g_rowSize);
        }

        if (done) break;
    }
}

void gather(int gather_id, int* gv, int id, int n_clients) {
    gv[id] += 1;  // mark progress
    while (true) {
        int done = 1;
        for (int i = 0; i < n_clients; i++)
            if (gv[i] < gather_id)
                done = 0;
        if (done) break;
        Sleep(0);
    }
}

int main(int argc, char* argv[])
{
    // Declare # of times the blur will be performed
    int iterations = atoi(argv[1]);
    printf("iterations: %d\n", iterations);
    // Declare static # of clients
    int n_clients = atoi(argv[2]);
    printf("n_clients: %d\n", n_clients);

    // Open the bitmap file
    // The variable file is now a pointer to the open bitmap file
    FILE *file = fopen(argv[3], "rb");
    
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
    g_data = (unsigned char *)malloc(fih.biSizeImage);

    fseek(file, fh.bfOffBits, SEEK_SET);
    fread(g_data, 1, fih.biSizeImage, file);
    fclose(file);

    // Create socket
    WSADATA w;
    WSAStartup(MAKEWORD(2,2), &w);

    struct addrinfo hints{}, *res;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = AI_PASSIVE;

    getaddrinfo(NULL, PORT, &hints, &res);

    SOCKET ls = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    bind(ls, res->ai_addr, (int)res->ai_addrlen);
    listen(ls, n_clients);

    // Spawn n clients
    for (int i = 0; i < n_clients; i++)
    {
        STARTUPINFOW si;
        PROCESS_INFORMATION pi;

        ZeroMemory(&si, sizeof(si));
        si.cb = sizeof(si);
        ZeroMemory(&pi, sizeof(pi));
        
        // Build command line for the child
        wchar_t cmd[256];
        swprintf(cmd, 256, L"calc.exe");

        // Start the child process
        CreateProcessW(NULL,     // No module name (use command line)
                        cmd,     // Command line
                        NULL,    // Process handle not inheritable
                        NULL,    // Thread handle not inheritable
                        FALSE,   // Set handle inheritance to FALSE
                        0,       // No creation flags
                        NULL,    // Use parent's environment block
                        NULL,    // Use parent's starting directory
                        &si,     // Pointer to STARTUPINFO structure
                        &pi)     // Pointer to PROCESS_INFORMATION structure
        ;
        // Close process and thread handles
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }

    // Add all the clients to a clients vector to reference them later
    vector<SOCKET> clients;
    for (int i = 0; i < n_clients; i++)
    {
        SOCKET client = accept(ls, NULL, NULL);
        clients.push_back(client);
    }

    int next_row = 0;
    init(&row_lock); 


    int* client_progress = new int[n_clients];
    for (int i = 0; i < n_clients; i++) {
        client_progress[i] = 0;
    }

    // For every iteration, do horizontal blur and then vertical blur w/ gathering between the 2 phases
    for (int i = 0; i < iterations; i++) {
        
        // Horizontal blur
        send_rows(clients, 0);

        // Gather horizontal
        for (int j = 0; j < n_clients; j++) {
            gather(i, client_progress, j, n_clients);
        }

        // Vertical blur
        send_rows(clients, 1);

        // Gather vertical
        for (int j = 0; j < n_clients; j++) {
            gather(i, client_progress, j, n_clients);
        }

    }

    // Create the output BMP file and write the output to the new file
    FILE *out = fopen("output.bmp", "wb");
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
    closesocket(ls); // Close the listener socket
    return 0;
};
