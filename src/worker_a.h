#pragma once

#include "worker_b.h"
#include <string>
#include <vector>

// Deserialize all page results received from a Worker A node
std::vector<PageResult> deserializeAllResults(const std::string& data);

void runWorkerA(int rank, int n, int m);
