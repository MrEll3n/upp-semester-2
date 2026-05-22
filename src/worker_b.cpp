#include "worker_b.h"
#include "mpi_common.h"
#include "utils.h"

#include <mpi.h>
#include <algorithm>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

// HTML parser

static int countTag(const std::string& html, const std::string& tag) {
    int count = 0;
    size_t pos = 0;
    while ((pos = html.find(tag, pos)) != std::string::npos) {
        count++;
        pos += tag.size();
    }
    return count;
}

static std::vector<std::string> extractHrefs(const std::string& html) {
    std::vector<std::string> hrefs;
    size_t pos = 0;

    while ((pos = html.find("<a ", pos)) != std::string::npos) {
        size_t tag_end = html.find('>', pos);
        if (tag_end == std::string::npos) break;

        size_t href_pos = html.find("href=", pos);
        if (href_pos == std::string::npos || href_pos > tag_end) {
            pos = tag_end;
            continue;
        }

        href_pos += 5; // skip "href="
        if (href_pos >= html.size()) break;

        char quote = html[href_pos];
        if (quote != '"' && quote != '\'') {
            pos = tag_end;
            continue;
        }

        size_t val_start = href_pos + 1;
        size_t val_end   = html.find(quote, val_start);
        if (val_end == std::string::npos) {
            pos = tag_end;
            continue;
        }

        std::string href = html.substr(val_start, val_end - val_start);
        if (!href.empty())
            hrefs.push_back(std::move(href));

        pos = tag_end;
    }
    return hrefs;
}

static std::vector<std::pair<int, std::string>> extractHeadings(const std::string& html) {
    // collect positions of all headings, then sort by occurrence
    std::vector<std::pair<size_t, int>> positions;

    for (int lvl = 1; lvl <= 6; lvl++) {
        std::string open = "<h" + std::to_string(lvl);
        size_t pos = 0;
        while ((pos = html.find(open, pos)) != std::string::npos) {
            // character after <h1 must be '>' or space
            size_t after = pos + open.size();
            if (after < html.size() && (html[after] == '>' || html[after] == ' '))
                positions.push_back({pos, lvl});
            pos += open.size();
        }
    }

    std::sort(positions.begin(), positions.end());

    std::vector<std::pair<int, std::string>> headings;
    for (auto& [pos, lvl] : positions) {
        size_t tag_end = html.find('>', pos);
        if (tag_end == std::string::npos) continue;

        std::string close = "</h" + std::to_string(lvl) + ">";
        size_t content_end = html.find(close, tag_end);
        if (content_end == std::string::npos) continue;

        std::string raw = html.substr(tag_end + 1, content_end - tag_end - 1);
        std::string text;
        bool in_tag = false;
        for (char c : raw) {
            if (c == '<') in_tag = true;
            else if (c == '>') in_tag = false;
            else if (!in_tag) text += c;
        }
        if (!text.empty())
            headings.push_back({lvl, text});
    }
    return headings;
}

// Result serialization for MPI transfer

std::string serializeResult(const PageResult& r) {
    std::ostringstream ss;
    ss << "URL "    << r.url    << "\n";
    ss << "IMAGES " << r.images << "\n";
    ss << "FORMS "  << r.forms  << "\n";
    ss << "LINKS "  << r.links  << "\n";
    for (const auto& href : r.hrefs)
        ss << "HREF " << href << "\n";
    for (const auto& [lvl, text] : r.headings)
        ss << "H" << lvl << " " << text << "\n";
    return ss.str();
}

PageResult deserializeResult(const std::string& data) {
    PageResult r;
    std::istringstream ss(data);
    std::string line;
    while (std::getline(ss, line)) {
        if (line.rfind("URL ",    0) == 0) r.url    = line.substr(4);
        else if (line.rfind("IMAGES ", 0) == 0) r.images = std::stoi(line.substr(7));
        else if (line.rfind("FORMS ",  0) == 0) r.forms  = std::stoi(line.substr(6));
        else if (line.rfind("LINKS ",  0) == 0) r.links  = std::stoi(line.substr(6));
        else if (line.rfind("HREF ",   0) == 0) r.hrefs.push_back(line.substr(5));
        else if (line.size() >= 3 && line[0] == 'H' && std::isdigit(line[1]) && line[2] == ' ')
            r.headings.push_back({line[1] - '0', line.substr(3)});
    }
    return r;
}

// Worker B main loop

void runWorkerB(int rank, int n, int m) {
    // determine which Worker A owns this Worker B
    int workerA_rank = (rank - n - 1) / m + 1;

    std::cout << "[Worker B rank=" << rank << "] Ready (owned by Worker A #" << workerA_rank << ")" << std::endl;

    while (true) {
        MPI_Status status;
        MPI_Probe(workerA_rank, MPI_ANY_TAG, MPI_COMM_WORLD, &status);

        if (status.MPI_TAG == TAG_TERMINATE) {
            char dummy;
            MPI_Recv(&dummy, 1, MPI_CHAR, workerA_rank, TAG_TERMINATE, MPI_COMM_WORLD, &status);
            std::cout << "[Worker B rank=" << rank << "] Terminated" << std::endl;
            break;
        }

        // receive URL
        int len;
        MPI_Get_count(&status, MPI_CHAR, &len);
        std::string url(len, '\0');
        MPI_Recv(url.data(), len, MPI_CHAR, workerA_rank, TAG_URL, MPI_COMM_WORLD, &status);

        std::cout << "[Worker B rank=" << rank << "] Downloading " << url << std::endl;

        // download and parse the page
        std::string html = utils::downloadHTML(url);

        PageResult result;
        result.url      = url;
        result.images   = countTag(html, "<img ");
        result.forms    = countTag(html, "<form ");
        result.links    = countTag(html, "<a ");
        result.hrefs    = extractHrefs(html);
        result.headings = extractHeadings(html);

        std::cout << "[Worker B rank=" << rank << "] Done: "
                  << result.images << " img, "
                  << result.links  << " links, "
                  << result.forms  << " forms, "
                  << result.hrefs.size() << " hrefs extracted" << std::endl;

        // send result back to Worker A
        std::string data = serializeResult(result);
        MPI_Send(data.c_str(), static_cast<int>(data.size()), MPI_CHAR,
                 workerA_rank, TAG_RESULT, MPI_COMM_WORLD);
    }
}
