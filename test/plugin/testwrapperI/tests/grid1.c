#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <sys/time.h>
#include <unistd.h>
#include "mpi.h"

/*
 * ./hellogrid <numOfIterations> <computeTime> <communicationSize>
 */
#include "mpi.h"
#include <stdio.h>
int main(int argc, char *argv[])
{
    int myid, numprocs, left, right;
    int buffer[10], buffer2[10];
    int i = 0;
    for(i = 0; i < 10; ++i) buffer2[i] = i;
    MPI_Request request;
    MPI_Status status;

    MPI_Init(&argc,&argv);
    MPI_Comm_size(MPI_COMM_WORLD, &numprocs);
    MPI_Comm_rank(MPI_COMM_WORLD, &myid);

    right = (myid + 1) % numprocs;
    left = myid - 1;
    if (left < 0)
        left = numprocs - 1;

    while(1){
        sleep(10);
    MPI_Irecv(buffer, 10, MPI_INT, left, 123, MPI_COMM_WORLD, &request);
    MPI_Send(buffer2, 10, MPI_INT, right, 123, MPI_COMM_WORLD);
    MPI_Wait(&request, &status);
    for(i = 0; i < 10; ++i)printf("%d", buffer[i]);
    }
    MPI_Finalize();
    return 0;
}
