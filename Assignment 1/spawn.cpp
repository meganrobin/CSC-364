#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdio.h>
#include <windows.h>
#include <stdint.h>
#include <stdlib.h>
#include <iostream>
#include <fstream>
#define STB_IMAGE_IMPLEMENTATION
#include <thread>
using namespace std;
#include <atomic>
#include <math.h>
#include <string.h>
#include <tchar.h>
#include <vector>

#pragma comment(lib, "Ws2_32.lib")

#define PORT "8081"
#define CONTRAST 100

// Global Variables
unsigned char *g_data;
int g_width;
int g_height;
int g_rowSize;

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

int main(int argc, char* argv[])
{
    // Extract command line args
    const char* executable = argv[1];
    int n_clients = atoi(argv[2]);

    // Open the bitmap file
    // The variable file is now a pointer to the open bitmap file
    FILE *file = fopen("./wolf.bmp", "rb");
    
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

    // Make a list(vector) of the clients to reference them later
    std::vector<SOCKET> clients;

    // Spawn n clients
    for (int i = 0; i < n_clients; i++)
    {
        STARTUPINFOW si;
        PROCESS_INFORMATION pi;

        ZeroMemory(&si, sizeof(si));
        si.cb = sizeof(si);
        ZeroMemory(&pi, sizeof(pi));
        
        // Build command line
        wchar_t cmd[256];
        swprintf(cmd, 256, L"%hs.exe %d", executable, CONTRAST);

        wprintf(L"Spawning client with command line: %ls\n", cmd);
        // Start child process
        if (!CreateProcessW(NULL,    // No module name (use command line)
                           cmd, // Command line
                           NULL,    // Process handle not inheritable
                           NULL,    // Thread handle not inheritable
                           FALSE,   // Set handle inheritance to FALSE
                           0,       // No creation flags
                           NULL,    // Use parent's environment block
                           NULL,    // Use parent's starting directory
                           &si,     // Pointer to STARTUPINFO structure
                           &pi)     // Pointer to PROCESS_INFORMATION structure
        )
        {
            printf("CreateProcess failed (%d).\n", GetLastError());
            return 1;
        }

        // Close process and thread handles.
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }

    for (int i = 0; i < n_clients; i++)
    {
        SOCKET client = accept(ls, NULL, NULL);
        clients.push_back(client);
    }

    int rowsPerClient = g_height / n_clients;

    for (int i = 0; i < n_clients; i++)
    {
        int startRow = i * rowsPerClient;
        int rows = (i == n_clients - 1) ? (g_height - startRow) : rowsPerClient;

        send(clients[i], (char*)&rows, sizeof(int), 0);
        send(clients[i], (char*)&g_width, sizeof(int), 0);

        send(clients[i],
            (char*)&g_data[startRow * g_rowSize],
            rows * g_rowSize,
            0);
    }

    for (int i = 0; i < n_clients; i++)
    {
        int startRow = i * rowsPerClient;
        int rows = (i == n_clients - 1) ? (g_height - startRow) : rowsPerClient;

        recv_all(clients[i],
            (char*)&g_data[startRow * g_rowSize],
            rows * g_rowSize);

        closesocket(clients[i]);
    }

    // Create the output BMP file
    // Write output
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
