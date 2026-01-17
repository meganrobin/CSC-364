#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdint.h>
#include <stdio.h>

#pragma comment(lib, "Ws2_32.lib")

#define PORT "8081"

static int send_all(SOCKET s, const char* buf, int len)
{
    int sent = 0;
    while (sent < len)
    {
        int n = send(s, buf + sent, len - sent, 0);
        if (n <= 0) return -1;
        sent += n;
    }
    return 0;
}

static int recv_all(SOCKET s, char* buf, int len)
{
    int recvd = 0;
    while (recvd < len)
    {
        int n = recv(s, buf + recvd, len - recvd, 0);
        if (n <= 0) return -1;
        recvd += n;
    }
    return 0;
}

static int send_msg(SOCKET s, const char* msg, uint32_t len)
{
    uint32_t nlen = htonl(len);
    send_all(s, (char*)&nlen, 4);
    return send_all(s, msg, len);
}

static int recv_msg(SOCKET s, char* buf, uint32_t* len)
{
    uint32_t nlen;
    recv_all(s, (char*)&nlen, 4);
    *len = ntohl(nlen);
    return recv_all(s, buf, *len);
}

int main()
{
    WSADATA w;
    WSAStartup(MAKEWORD(2,2), &w);

    struct addrinfo hints{}, *res;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    getaddrinfo("127.0.0.1", PORT, &hints, &res);
    SOCKET s = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    connect(s, res->ai_addr, (int)res->ai_addrlen);

    char buf[4096];
    uint32_t len;

    recv_msg(s, buf, &len); // READY
    printf("%.*s", len, buf);

    for (int i = 0; i < 10; i++)
    {
        char msg[64];
        int n = sprintf(msg, "PING %d", i);
        send_msg(s, msg, n);

        recv_msg(s, buf, &len);
        printf("Received: %.*s\n", len, buf);
    }

    closesocket(s);
}