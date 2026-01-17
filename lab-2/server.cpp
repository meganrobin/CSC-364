#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <thread>
#include <stdint.h>
#include <stdio.h>

#pragma comment(lib, "Ws2_32.lib")

#define PORT "8081"

// Helper funct to 
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
    if (send_all(s, (char*)&nlen, 4) < 0) return -1;
    return send_all(s, msg, len);
}

static int recv_msg(SOCKET s, char* buf, uint32_t* len)
{
    uint32_t nlen;
    if (recv_all(s, (char*)&nlen, 4) < 0) return -1;
    *len = ntohl(nlen);
    return recv_all(s, buf, *len);
}

// Continuously send messages from "from" to "to"
static void relay(SOCKET from, SOCKET to)
{
    char buf[4096];
    uint32_t len;

    while (1)
    {
        if (recv_msg(from, buf, &len) < 0) break;
        if (send_msg(to, buf, len) < 0) break;
    }
}

int main()
{
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
    listen(ls, 2);

    printf("Waiting for client A\n");
    SOCKET a = accept(ls, NULL, NULL);

    printf("Waiting for client B\n");
    SOCKET b = accept(ls, NULL, NULL);

    send_msg(a, "READY\n", 6);
    send_msg(b, "READY\n", 6);

    std::thread t1(relay, a, b);
    std::thread t2(relay, b, a);

    t1.join();
    t2.join();

    closesocket(a);
    closesocket(b);
    closesocket(ls);
}