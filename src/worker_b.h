#pragma once

#include <string>
#include <vector>
#include <utility>

// Result of parsing a single page
struct PageResult {
    std::string url;
    int images = 0;
    int forms  = 0;
    int links  = 0;
    std::vector<std::string> hrefs;                    // extracted href values
    std::vector<std::pair<int, std::string>> headings; // (level, text)
};

// Serialization/deserialization for MPI transfer
std::string serializeResult(const PageResult& r);
PageResult  deserializeResult(const std::string& data);

void runWorkerB(int rank, int n, int m);
