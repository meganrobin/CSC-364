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

#pragma comment(lib, "Ws2_32.lib")


// Timing stuff
static uint64_t now_us(void)
{
    static LARGE_INTEGER f;
    static int init = 0;
    if (!init) { QueryPerformanceFrequency(&f); init = 1; }
    LARGE_INTEGER t; QueryPerformanceCounter(&t);
    return (uint64_t)((t.QuadPart * 1000000ULL) / (uint64_t)f.QuadPart);
}

// Connect TCP
static SOCKET connect_tcp(const char* host, const char* port)
{
    struct addrinfo hints, *res = 0, *p = 0;
    ZeroMemory(&hints, sizeof(hints));
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    if (getaddrinfo(host, port, &hints, &res) != 0)
        return INVALID_SOCKET;

    for (p = res; p; p = p->ai_next)
    {
        SOCKET s = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (s == INVALID_SOCKET) continue;

        if (connect(s, p->ai_addr, (int)p->ai_addrlen) == 0)
        {
            freeaddrinfo(res);
            return s;
        }
        closesocket(s);
    }
    freeaddrinfo(res);
    return INVALID_SOCKET;
}

// Helper funct: Send all bytes
static int send_all(SOCKET s, const char* buf, int len)
{
    int sent = 0;
    while (sent < len)
    {
        int n = send(s, buf + sent, len - sent, 0);
        sent += n;
    }
    return 0;
}

// Helper funct: Receive all bytes
static int recv_all(SOCKET s, char* buf, int len)
{
    int recvd = 0;
    while (recvd < len)
    {
        int n = recv(s, buf + recvd, len - recvd, 0);
        recvd += n;
    }
    return 0;
}


// Main funct
int main(int argc, char** argv)
{
    const char* host = argv[1];
    const char* port = argv[2];
    int pings = atoi(argv[3]);
    int ping_bytes = atoi(argv[4]);
    int bulk_bytes = atoi(argv[5]);

    WSADATA w;
    WSAStartup(MAKEWORD(2,2), &w);

    SOCKET s = connect_tcp(host, port);
    if (s == INVALID_SOCKET) return -1;

    char* buf = (char*)malloc(bulk_bytes);
    for (int i = 0; i < bulk_bytes; i++) buf[i] = (char)(i & 0xff);

    uint64_t min = UINT64_MAX, max = 0, sum = 0;

    // Send short pings
    for (int i = 0; i < pings; i++)
    {
        uint64_t t0 = now_us();
        send_all(s, buf, ping_bytes);
        recv_all(s, buf, ping_bytes);
        uint64_t t1 = now_us();


        uint64_t rtt = t1 - t0;
        if (rtt < min) min = rtt;
        if (rtt > max) max = rtt;
        sum += rtt;
    }


    // Send bulk bytes
    uint64_t t0 = now_us();
    send_all(s, buf, bulk_bytes);
    recv_all(s, buf, bulk_bytes);
    uint64_t t1 = now_us();


    double seconds = (double)(t1 - t0) / 1e6;
    double mbps = (bulk_bytes / (1024.0 * 1024.0)) / seconds;

    // Output the results
    printf("Minimum round-trip time: %llu microseconds\n", (unsigned long long)min);
    printf("Average round-trip time: %llu microseconds\n", (unsigned long long)(sum / pings));
    printf("Maximum round-trip time: %llu microseconds\n", (unsigned long long)max);
    printf("Throughput: %.2f MB/s\n", mbps);

    closesocket(s);
    free(buf);
    return 0;

}