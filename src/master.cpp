#include "master.h"
#include "mpi_common.h"
#include "worker_a.h"
#include "server.h"

#include <mpi.h>
#include <algorithm>
#include <chrono>
#include <ctime>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace fs = std::filesystem;

// URL / path helpers

static std::string extractPath(const std::string& url) {
    size_t scheme_end = url.find("://");
    if (scheme_end == std::string::npos) return url;
    size_t path_start = url.find('/', scheme_end + 3);
    return (path_start == std::string::npos) ? "/" : url.substr(path_start);
}

static std::string normalizePath(const std::string& path) {
    std::vector<std::string> segs;
    size_t pos = 1;
    while (pos <= path.size()) {
        size_t next = path.find('/', pos);
        if (next == std::string::npos) next = path.size();
        std::string seg = path.substr(pos, next - pos);
        if (seg == "..") { if (!segs.empty()) segs.pop_back(); }
        else if (seg != "." && !seg.empty()) segs.push_back(seg);
        pos = next + 1;
    }
    std::string result;
    for (const auto& s : segs) result += "/" + s;
    return result.empty() ? "/" : result;
}

static std::string resolveHrefPath(const std::string& href, const std::string& pageURL) {
    if (href.empty() || href[0] == '#' || href[0] == '?') return "";
    size_t scheme_end = pageURL.find("://");
    if (scheme_end == std::string::npos) return "";
    size_t host_end = pageURL.find('/', scheme_end + 3);
    std::string base = (host_end == std::string::npos) ? pageURL : pageURL.substr(0, host_end);

    std::string rawPath;
    if (href.find("://") != std::string::npos) rawPath = extractPath(href);
    else if (href[0] == '/') rawPath = href;
    else {
        size_t slash = pageURL.rfind('/');
        std::string dir = (slash != std::string::npos && slash >= base.size())
                          ? pageURL.substr(host_end, slash - host_end + 1)
                          : "/";
        rawPath = dir + href;
    }
    return normalizePath(rawPath);
}

static std::string trimWhitespace(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

static std::string makeSafeURL(const std::string& url) {
    // strip scheme, replace special characters with '_'
    size_t scheme_end = url.find("://");
    std::string s = (scheme_end != std::string::npos) ? url.substr(scheme_end + 3) : url;
    s = trimWhitespace(s);
    for (char& c : s)
        if (c == '.' || c == '/' || c == ':') c = '_';
    while (!s.empty() && s.back() == '_') s.pop_back();
    return s;
}

static std::string currentTimestamp(const char* fmt) {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    char buf[64];
    std::strftime(buf, sizeof(buf), fmt, std::localtime(&t));
    return buf;
}

// File output

static void saveResults(const std::string& baseURL,
                        const std::vector<PageResult>& results,
                        const std::string& startTime) {
    std::string dir = "results/"
                    + currentTimestamp("%Y_%m_%d_%H_%M")
                    + "_" + makeSafeURL(baseURL);
    fs::create_directories(dir);

    // map.txt — first all nodes (URIs), then edges
    {
        std::unordered_set<std::string> crawledPaths;
        for (const auto& r : results)
            crawledPaths.insert(extractPath(r.url));

        std::set<std::pair<std::string,std::string>> edges;
        for (const auto& r : results) {
            std::string srcPath = extractPath(r.url);
            for (const auto& href : r.hrefs) {
                std::string tgtPath = resolveHrefPath(href, r.url);
                if (!tgtPath.empty() && tgtPath != srcPath && crawledPaths.count(tgtPath))
                    edges.insert({srcPath, tgtPath});
            }
        }

        std::ofstream f(dir + "/map.txt");
        for (const auto& r : results)
            f << "\"" << extractPath(r.url) << "\"\n";
        for (const auto& [src, tgt] : edges)
            f << "\"" << src << "\" \"" << tgt << "\"\n";
    }

    // content.txt
    {
        std::ofstream f(dir + "/content.txt");
        for (const auto& r : results) {
            f << "\"" << extractPath(r.url) << "\"\n";
            f << "IMAGES " << r.images << "\n";
            f << "LINKS "  << r.links  << "\n";
            f << "FORMS "  << r.forms  << "\n";
            for (const auto& [lvl, text] : r.headings)
                f << std::string(lvl, '-') << " " << text << "\n";
            f << "\n";
        }
    }

    // log.txt
    {
        std::ofstream f(dir + "/log.txt");
        f << startTime << "\n";
        f << currentTimestamp("%Y-%m-%d %H:%M:%S") << "\n";
        f << "OK\n";
    }
}

// HTML response

static std::string buildHTMLResponse(const std::vector<PageResult>& results) {
    std::ostringstream ss;
    ss << "<h2>Crawling complete</h2>";
    ss << "<p>Crawled " << results.size() << " page(s).</p><ul>";
    for (const auto& r : results) {
        ss << "<li>" << r.url
           << " &mdash; " << r.images << " img, "
           << r.links  << " links, "
           << r.forms  << " forms</li>";
    }
    ss << "</ul>";
    return ss.str();
}

// Master main logic

void runMaster(int n, int m) {
    CServer svr;
    if (!svr.Init("./data", "0.0.0.0", 8001)) {
        std::cerr << "[Master] Failed to initialize server!" << std::endl;
        return;
    }

    std::cout << "[Master] HTTP server listening on http://localhost:8001/" << std::endl;

    svr.RegisterFormCallback([n](const std::vector<std::string>& URLs, std::string& vystup) {
        if (URLs.empty()) {
            vystup = "<p>No URLs provided.</p>";
            return;
        }

        // trim whitespace/CRLF from all URLs
        std::deque<std::string> urlQueue;
        for (const auto& url : URLs) {
            std::string t = trimWhitespace(url);
            if (!t.empty()) urlQueue.push_back(t);
        }

        std::string startTime = currentTimestamp("%Y-%m-%d %H:%M:%S");
        std::cout << "[Master] Received " << urlQueue.size() << " URL(s) — dispatching one at a time" << std::endl;

        std::unordered_set<int> freeWorkers, busyWorkers;
        for (int a = 1; a <= n; a++) freeWorkers.insert(a);

        std::unordered_map<int, std::string> workerURL; // worker rank -> assigned URL
        std::vector<PageResult> allResults;

        while (!urlQueue.empty() || !busyWorkers.empty()) {

            // send one URL to each free Worker A
            while (!urlQueue.empty() && !freeWorkers.empty()) {
                std::string url = urlQueue.front(); urlQueue.pop_front();
                int worker = *freeWorkers.begin();
                freeWorkers.erase(freeWorkers.begin());
                busyWorkers.insert(worker);
                workerURL[worker] = url;

                std::cout << "[Master] -> Worker A #" << worker << " : " << url << std::endl;
                MPI_Send(url.c_str(), static_cast<int>(url.size()),
                         MPI_CHAR, worker, TAG_URL, MPI_COMM_WORLD);
            }

            // wait for any Worker A to finish
            if (!busyWorkers.empty()) {
                MPI_Status status;
                MPI_Probe(MPI_ANY_SOURCE, TAG_DONE, MPI_COMM_WORLD, &status);
                int len;
                MPI_Get_count(&status, MPI_CHAR, &len);
                std::string data(len, '\0');
                MPI_Recv(data.data(), len, MPI_CHAR, status.MPI_SOURCE, TAG_DONE,
                         MPI_COMM_WORLD, &status);

                auto chunk = deserializeAllResults(data);
                std::cout << "[Master] <- Worker A #" << status.MPI_SOURCE
                          << " returned " << chunk.size() << " page(s)" << std::endl;

                if (!chunk.empty()) {
                    saveResults(workerURL[status.MPI_SOURCE], chunk, startTime);
                    std::cout << "[Master] Results saved to results/" << std::endl;
                }
                allResults.insert(allResults.end(), chunk.begin(), chunk.end());

                workerURL.erase(status.MPI_SOURCE);
                busyWorkers.erase(status.MPI_SOURCE);
                freeWorkers.insert(status.MPI_SOURCE);
            }
        }

        std::cout << "[Master] Crawl complete — " << allResults.size() << " pages total" << std::endl;

        vystup = buildHTMLResponse(allResults);
    });

    svr.Run();
}
