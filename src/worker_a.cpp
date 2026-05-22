#include "worker_a.h"
#include "mpi_common.h"

#include <mpi.h>
#include <deque>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

// URL helpers

static std::string extractBase(const std::string& url) {
    // returns "scheme://host" part of the URL
    size_t scheme_end = url.find("://");
    if (scheme_end == std::string::npos) return "";
    size_t host_end = url.find('/', scheme_end + 3);
    return (host_end == std::string::npos) ? url : url.substr(0, host_end);
}

static std::string normalizeURL(const std::string& url) {
    // collapse /./ and /../ segments in the path portion of an absolute URL
    size_t scheme_end = url.find("://");
    if (scheme_end == std::string::npos) return url;
    size_t path_start = url.find('/', scheme_end + 3);
    if (path_start == std::string::npos) return url;

    std::string origin = url.substr(0, path_start);
    std::string path   = url.substr(path_start);

    std::vector<std::string> segments;
    size_t pos = 1; // skip leading '/'
    while (pos <= path.size()) {
        size_t next = path.find('/', pos);
        if (next == std::string::npos) next = path.size();
        std::string seg = path.substr(pos, next - pos);
        if (seg == "..") {
            if (!segments.empty()) segments.pop_back();
        } else if (seg != "." && !seg.empty()) {
            segments.push_back(seg);
        }
        pos = next + 1;
    }

    std::string result = origin;
    for (const auto& s : segments)
        result += "/" + s;
    if (result == origin) result += "/"; // keep bare host as host/

    // strip default document so /index.html canonicalizes to /
    for (const std::string idx : {"/index.html", "/index.htm", "/index.php"}) {
        if (result.size() > idx.size() &&
            result.compare(result.size() - idx.size(), idx.size(), idx) == 0) {
            result.erase(result.size() - idx.size() + 1); // leave the trailing slash
            break;
        }
    }
    return result;
}

static std::string resolveURL(const std::string& href, const std::string& base,
                              const std::string& currentURL = "") {
    if (href.find("://") != std::string::npos) return normalizeURL(href);
    if (href.empty() || href[0] == '#' || href[0] == '?') return "";
    if (href[0] == '/') return normalizeURL(base + href);

    // relative URL — resolve against the directory of currentURL
    if (!currentURL.empty()) {
        size_t slash = currentURL.rfind('/');
        if (slash != std::string::npos && slash >= base.size())
            return normalizeURL(currentURL.substr(0, slash + 1) + href);
        return normalizeURL(base + "/" + href);
    }
    return "";
}

// MPI helpers

static std::string mpiRecvString(int source, int tag) {
    MPI_Status status;
    MPI_Probe(source, tag, MPI_COMM_WORLD, &status);
    int len;
    MPI_Get_count(&status, MPI_CHAR, &len);
    std::string buf(len, '\0');
    MPI_Recv(buf.data(), len, MPI_CHAR, source, tag, MPI_COMM_WORLD, &status);
    return buf;
}

static void mpiSendString(const std::string& s, int dest, int tag) {
    MPI_Send(s.c_str(), static_cast<int>(s.size()), MPI_CHAR, dest, tag, MPI_COMM_WORLD);
}

// Result serialization

static std::string serializeAllResults(const std::vector<PageResult>& results) {
    std::ostringstream ss;
    for (const auto& r : results)
        ss << serializeResult(r) << "---\n";
    return ss.str();
}

std::vector<PageResult> deserializeAllResults(const std::string& data) {
    std::vector<PageResult> results;
    std::istringstream ss(data);
    std::string chunk, line;
    while (std::getline(ss, line)) {
        if (line == "---") {
            if (!chunk.empty()) {
                results.push_back(deserializeResult(chunk));
                chunk.clear();
            }
        } else {
            chunk += line + "\n";
        }
    }
    return results;
}

// Worker A main loop

void runWorkerA(int rank, int n, int m) {
    // Worker B rank range owned by this Worker A
    int first_b = n + 1 + (rank - 1) * m;
    int last_b  = first_b + m - 1;

    std::cout << "[Worker A #" << rank << "] Ready (owns Worker B ranks "
              << first_b << "-" << last_b << ")" << std::endl;

    // outer loop — handle multiple crawl requests from Master
    while (true) {
        // wait for TAG_URL (new crawl) or TAG_TERMINATE (shutdown) from Master
        MPI_Status probe_status;
        MPI_Probe(0, MPI_ANY_TAG, MPI_COMM_WORLD, &probe_status);

        if (probe_status.MPI_TAG == TAG_TERMINATE) {
            char dummy;
            MPI_Recv(&dummy, 1, MPI_CHAR, 0, TAG_TERMINATE, MPI_COMM_WORLD, &probe_status);
            std::cout << "[Worker A #" << rank << "] Terminating Worker B nodes and exiting" << std::endl;
            for (int b = first_b; b <= last_b; b++)
                MPI_Send(&dummy, 1, MPI_CHAR, b, TAG_TERMINATE, MPI_COMM_WORLD);
            break;
        }

        // receive a single URL from Master
        std::string startURL = mpiRecvString(0, TAG_URL);
        // trim any accidental whitespace
        while (!startURL.empty() && (startURL.back() == '\n' || startURL.back() == '\r' || startURL.back() == ' '))
            startURL.pop_back();

        if (startURL.empty()) {
            std::cout << "[Worker A #" << rank << "] Received empty URL, skipping" << std::endl;
            mpiSendString("", 0, TAG_DONE);
            continue;
        }

        std::deque<std::string> queue;
        queue.push_back(startURL);

        // URL prefix: all accepted URLs must start with this
        std::string prefix = queue.front();
        std::string base   = extractBase(prefix);

        std::cout << "[Worker A #" << rank << "] Got " << queue.size()
                  << " URL(s), prefix: " << prefix << std::endl;

        std::unordered_set<std::string> visited;
        std::vector<PageResult>         results;

        // mark start URL as visited immediately so it isn't enqueued again
        visited.insert(queue.front());

        std::unordered_set<int> free_workers;
        std::unordered_set<int> busy_workers;
        for (int b = first_b; b <= last_b; b++)
            free_workers.insert(b);

        while (!queue.empty() || !busy_workers.empty()) {

            // dispatch URLs to free Worker B nodes
            while (!queue.empty() && !free_workers.empty()) {
                std::string url = queue.front(); queue.pop_front();

                int worker = *free_workers.begin();
                free_workers.erase(free_workers.begin());
                busy_workers.insert(worker);

                std::cout << "[Worker A #" << rank << "] -> Worker B rank=" << worker
                          << " : " << url << std::endl;
                mpiSendString(url, worker, TAG_URL);
            }

            // wait for a result from any busy Worker B
            if (!busy_workers.empty()) {
                MPI_Status status;
                MPI_Probe(MPI_ANY_SOURCE, TAG_RESULT, MPI_COMM_WORLD, &status);
                int len;
                MPI_Get_count(&status, MPI_CHAR, &len);
                std::string data(len, '\0');
                MPI_Recv(data.data(), len, MPI_CHAR, status.MPI_SOURCE, TAG_RESULT,
                         MPI_COMM_WORLD, &status);

                PageResult result = deserializeResult(data);
                if (result.url.find(prefix) == 0)
                    results.push_back(result);

                std::cout << "[Worker A #" << rank << "] <- rank=" << status.MPI_SOURCE
                          << " : " << result.url
                          << " (" << result.images << " img, "
                          << result.links << " links, "
                          << result.forms << " forms)" << std::endl;

                // enqueue new URLs that match the prefix and haven't been visited
                int newURLs = 0;
                for (const auto& href : result.hrefs) {
                    std::string resolved = resolveURL(href, base, result.url);
                    if (resolved.empty())
                        std::cout << "[Worker A #" << rank << "] SKIP (unresolvable): " << href << std::endl;
                    else if (resolved.find(prefix) != 0)
                        std::cout << "[Worker A #" << rank << "] SKIP (prefix mismatch): " << resolved << std::endl;
                    else if (visited.count(resolved))
                        std::cout << "[Worker A #" << rank << "] SKIP (already visited): " << resolved << std::endl;
                    else {
                        queue.push_back(resolved);
                        visited.insert(resolved);
                        newURLs++;
                    }
                }
                if (newURLs > 0)
                    std::cout << "[Worker A #" << rank << "] Queued " << newURLs
                              << " new URL(s), queue size: " << queue.size() << std::endl;

                busy_workers.erase(status.MPI_SOURCE);
                free_workers.insert(status.MPI_SOURCE);
            }
        }

        std::cout << "[Worker A #" << rank << "] Done — crawled " << results.size()
                  << " page(s)" << std::endl;

        // send collected results to Master
        mpiSendString(serializeAllResults(results), 0, TAG_DONE);
    }
}
