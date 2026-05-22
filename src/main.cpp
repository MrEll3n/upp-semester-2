#include <string>
#include <vector>
#include <iostream>

#include <mpi.h>

#include "master.h"
#include "worker_a.h"
#include "worker_b.h"

int main(int argc, char** argv) {

    MPI_Init(&argc, &argv);

    int rank, size;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    // parse -n and -m from arguments
    int n = 1, m = 1;
    for (int i = 1; i < argc - 1; i++) {
        if (std::string(argv[i]) == "-n") n = std::stoi(argv[i + 1]);
        if (std::string(argv[i]) == "-m") m = std::stoi(argv[i + 1]);
    }

    // expected process count: 1 + n + n*m
    if (rank == 0 && size != 1 + n + n * m) {
        std::cerr << "Error: expected " << 1 + n + n * m
                  << " processes, got " << size << std::endl;
        MPI_Abort(MPI_COMM_WORLD, 1);
    }

    if (rank == 0)
        runMaster(n, m);
    else if (rank <= n)
        runWorkerA(rank, n, m);
    else
        runWorkerB(rank, n, m);

    MPI_Finalize();
    return EXIT_SUCCESS;
}
