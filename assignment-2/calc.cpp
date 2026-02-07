#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <iostream>
#include <algorithm>
using namespace std;

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

        if (row == -1) // Stop client if the row to operate on is "-1"
            break;

        int pass; // 0 if horizontal, 1 if vertical
        recv_all(sock, (char*)&pass, sizeof(int));
        int width;
        recv_all(sock, (char*)&width, sizeof(int));
        int height;
        recv_all(sock, (char*)&height, sizeof(int));

        int rowSize = (width * 3 + 3) & ~3;
        unsigned char* img = new unsigned char[rowSize * height];
        recv_all(sock, (char*)img, rowSize * height);

        unsigned char* out_img = new unsigned char[rowSize];

        for (int x = 0; x < width; x++) {
            for (int c = 0; c < 3; c++) {
                double sum = 0.0;

                auto sample = [&](int r, int cx) {
                    r = clamp(r, 0, height - 1);
                    cx = clamp(cx, 0, width - 1);
                    return img[r * rowSize + cx * 3 + c];
                };

                if (pass == 0) { // horizontal
                    sum += sample(row, x) * 0.399050;
                    sum += (sample(row, x-1) + sample(row, x+1)) * 0.242036;
                    sum += (sample(row, x-2) + sample(row, x+2)) * 0.054005;
                    sum += (sample(row, x-3) + sample(row, x+3)) * 0.004433;
                } else { // vertical
                    sum += sample(row, x) * 0.399050;
                    sum += (sample(row-1, x) + sample(row+1, x)) * 0.242036;
                    sum += (sample(row-2, x) + sample(row+2, x)) * 0.054005;
                    sum += (sample(row-3, x) + sample(row+3, x)) * 0.004433;
                }

                out_img[x * 3 + c] = (unsigned char)clamp((int)sum, 0, 255);
            }
        }

        send(sock, (char*)out_img, rowSize, 0);
        delete[] out_img;
    }
    
    closesocket(sock);
    return 0;
}