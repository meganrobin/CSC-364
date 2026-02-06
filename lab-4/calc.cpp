#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#pragma comment(lib, "Ws2_32.lib")

#define PORT 8081

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
    printf("Calculator client started.\n");

    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);

    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);

    sockaddr_in server{};
    server.sin_family = AF_INET;
    server.sin_port = htons(PORT);
    inet_pton(AF_INET, "127.0.0.1", &server.sin_addr);

    connect(sock, (sockaddr*)&server, sizeof(server));

    while (true)
    {
        int row;
        recv_all(sock, (char*)&row, sizeof(int));

        if (row == -1) // Stop client
            break;

        int pixelsPerRow;
        recv_all(sock, (char*)&pixelsPerRow, sizeof(int));

        int rowSize = pixelsPerRow * 3;
        unsigned char* buffer = new unsigned char[rowSize];

        recv_all(sock, (char*)buffer, rowSize);

        // Blur logic
        for (int x = 0; x < pixelsPerRow; x += 5)
        {
            for (int c = 0; c < 3; c++) // For R, G, and B
            {
                int sum = 0;
                int count = 0;

                for (int i = 0; i < 5 && x + i < pixelsPerRow; i++)
                {
                    sum += buffer[(x + i) * 3 + c];
                    count++;
                }

                unsigned char avg = (unsigned char)(sum / count);

                for (int i = 0; i < 5 && x + i < pixelsPerRow; i++)
                {
                    buffer[(x + i) * 3 + c] = avg;
                }
            }
        }
        send(sock, (char*)buffer, rowSize, 0);
        delete[] buffer;
    }
    closesocket(sock);

    return 0;
}