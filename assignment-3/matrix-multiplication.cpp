// The strategy I used for my matrix multiplication program is to divide the rows of Matrix A up between the processes, 
// and broadcast Matrix B to all processes. This is because with square matrix multiplication, for each resulting number in matrix C, you need to multiply a row of A
// by a column of B. So, if I give each process an equally(or as equally as possible) divided amount of rows and access to all columns, then the time the program takes will decrease 
// as nproc increases.
// I use MPI_Gatherv at the end to collect the rows of C from all the processes. Rank 0 prints the # of processes and the total time taken.

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <iostream>
#include <algorithm>
#include <mpi.h>
#include <vector>
#include <cmath>
#include <cstring>

using namespace std;
#pragma comment(lib, "Ws2_32.lib")

int main(int argc, char **argv)
{

    MPI_Init(&argc, &argv);

    int rank, size;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size); // size is the number of processes
    
    int N = atoi(argv[1]); // NxN matrix

    vector<float> A(N * N);
    vector<float> B(N * N);
    vector<float> C(N * N);
    // On rank == 0, fill the 2 NxN matrices A and B w/ float numbers between 0 and 1
    if (rank == 0) {
        printf("Number of processes: %d\n", size);
        for (int i = 0; i < N * N; i++)
        {
            A[i] = rand() / (float)RAND_MAX;
            B[i] = rand() / (float)RAND_MAX;
        }
    }

    // Broadcast Matrix B to all processes
    MPI_Bcast(
        B.data(), 
        N * N, 
        MPI_FLOAT, 
        0, 
        MPI_COMM_WORLD
    );

    // Calculate row distribution to the processes
    vector<int> sendcounts(size), displs(size);

    int rows_per_proc = N / size;
    int remainder = N % size;

    int offset = 0;
    for (int i = 0; i < size; i++)
    {
        int rows = rows_per_proc + (i < remainder ? 1 : 0);
        sendcounts[i] = rows * N;
        displs[i] = offset;
        offset += rows * N;
    }

    int local_rows = sendcounts[rank] / N;

    vector<float> local_A(sendcounts[rank]);
    vector<float> local_C(sendcounts[rank]);

    // Scatter rows of A
    MPI_Scatterv(
        rank == 0 ? A.data() : nullptr,
        sendcounts.data(),
        displs.data(),
        MPI_FLOAT,
        local_A.data(),
        sendcounts[rank],
        MPI_FLOAT,
        0,
        MPI_COMM_WORLD
    );

    MPI_Barrier(MPI_COMM_WORLD);
    // Start the timer
    double start_time = MPI_Wtime();

    // Square matrix multiplication at local levl
    for (int i = 0; i < local_rows; i++)
    {
        for (int j = 0; j < N; j++)
        {
            float sum = 0.0f;
            for (int k = 0; k < N; k++)
            {
                sum += local_A[i * N + k] * B[k * N + j];
            }
            local_C[i * N + j] = sum;
        }
    }

    MPI_Barrier(MPI_COMM_WORLD);
    double end_time = MPI_Wtime();

    // Gather results
    MPI_Gatherv(
        local_C.data(),
        sendcounts[rank],
        MPI_FLOAT,
        rank == 0 ? C.data() : nullptr,
        sendcounts.data(),
        displs.data(),
        MPI_FLOAT,
        0,
        MPI_COMM_WORLD
    );

    if (rank == 0)
    {
        printf("Total time taken: %f", (end_time - start_time));
    }

    MPI_Finalize();   
}
