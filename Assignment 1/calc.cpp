#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#pragma comment(lib, "Ws2_32.lib")

#define PORT 8081

int adjust(int x, int c)
{
    double f = (259.0 * (c + 255)) / (255.0 * (259 - c));
    int y = (int)(f * (x - 128) + 128);
    if (y < 0) y = 0;
    if (y > 255) y = 255;
    return y;
}

int main(int argc, char* argv[])
{
    printf("Calculator client started.\n");

    int contrast = atoi(argv[1]);

    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);

    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);

    sockaddr_in server{};
    server.sin_family = AF_INET;
    server.sin_port = htons(PORT);
    inet_pton(AF_INET, "127.0.0.1", &server.sin_addr);

    connect(sock, (sockaddr*)&server, sizeof(server));

    int rows, pixelsPerRow;
    recv(sock, (char*)&rows, sizeof(int), 0);
    recv(sock, (char*)&pixelsPerRow, sizeof(int), 0);

    int rowSize = pixelsPerRow * 3;
    int totalBytes = rows * rowSize;

    unsigned char* buffer = new unsigned char[totalBytes];
    recv(sock, (char*)buffer, totalBytes, 0);

    for (int i = 0; i < totalBytes; i++)
        buffer[i] = (unsigned char)adjust(buffer[i], contrast);

    send(sock, (char*)buffer, totalBytes, 0);

    delete[] buffer;
    closesocket(sock);

    return 0;
}