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
// echoserver_win.c
// Usage: echoserver_win <port>

#pragma comment(lib, "Ws2_32.lib")
int main(int argc, char** argv)
{
    int port = atoi(argv[1]);

    // WSA stands for "Windows Socket API"
    WSADATA w;
    WSAStartup(MAKEWORD(2, 2), &w); // MAKEWORD just says get me WSA in version 2.2

    SOCKET ls = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    struct sockaddr_in addr = { 0 };
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);

    bind(ls, (struct sockaddr*)&addr, sizeof(addr));
    listen(ls, 5);

    printf("Echo server listening on port %d\n", port);

    for (;;)
    {
        SOCKET c = accept(ls, NULL, NULL);
        if (c == INVALID_SOCKET) continue;

        char buf[4096];
        int n;
        while ((n = recv(c, buf, sizeof(buf), 0)) > 0)
        {
            send(c, buf, n, 0);
        }
        closesocket(c);
    }
}