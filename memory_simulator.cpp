#include <iostream>
#include <vector>
#include <map>
#include <queue>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <string>
#include <random>
#include <climits>
#include <chrono>
#include <algorithm>

enum Protection { READ_ONLY, READ_WRITE };

struct Page {
    int frame_number = -1;
    bool present = false;
    Protection protection = READ_WRITE;
    int last_access = 0;
};

struct Segment {
    int base_address;
    int limit;
    Protection protection;
};

class PageTable {
public:
    std::vector<Page> pages;
    int page_size;

    PageTable() : page_size(1000) {}

    PageTable(int numPages, int pageSize) : page_size(pageSize) {
        pages.resize(numPages);
        for (auto& p : pages) {
            p.present = rand() % 2;
            if (p.present) p.frame_number = rand() % 100;
            p.protection = (rand() % 2) ? READ_WRITE : READ_ONLY;
        }
    }

    int getFrameNumber(int pageNum, int time, Protection accessType) {
        if (pageNum < 0 || pageNum >= (int)pages.size()) {
            std::cout << "Page Fault: Invalid page number " << pageNum << "\n";
            return -1;
        }
        if (!pages[pageNum].present) {
            std::cout << "Page Fault: Page " << pageNum << " not in memory\n";
            return -1;
        }
        if (accessType == READ_WRITE && pages[pageNum].protection == READ_ONLY) {
            std::cout << "Protection Violation: Cannot write to read-only page\n";
            return -1;
        }
        pages[pageNum].last_access = time;
        return pages[pageNum].frame_number;
    }

    void setFrame(int pageNum, int frame, Protection prot) {
        if (pageNum >= 0 && pageNum < (int)pages.size()) {
            pages[pageNum].frame_number = frame;
            pages[pageNum].present = true;
            pages[pageNum].protection = prot;
        }
    }
};

class DirectoryTable {
public:
    std::map<int, PageTable> directories;
    int page_size;

    DirectoryTable(int pageSize) : page_size(pageSize) {}

    void addDirectory(int dirIndex, int numPages) {
        directories[dirIndex] = PageTable(numPages, page_size);
    }

    int getFrame(int dirIndex, int pageNum, int time, Protection access) {
        if (directories.find(dirIndex) == directories.end()) {
            std::cout << "Directory Fault: Invalid directory " << dirIndex << "\n";
            return -1;
        }
        return directories[dirIndex].getFrameNumber(pageNum, time, access);
    }
};

class PhysicalMemory {
public:
    int num_frames;
    std::vector<bool> free_frames;
    std::queue<int> fifo_queue;
    int time = 0;

    PhysicalMemory(int frames) : num_frames(frames) {
        free_frames.resize(frames, true);
    }

    int allocateFrame() {
        for (int i = 0; i < num_frames; ++i) {
            if (free_frames[i]) {
                free_frames[i] = false;
                fifo_queue.push(i);
                return i;
            }
        }
        // FIFO replacement
        int frame = fifo_queue.front();
        fifo_queue.pop();
        fifo_queue.push(frame);
        return frame;
    }

    int allocateFrameLRU(std::map<int, PageTable>& pageTables) {
        for (int i = 0; i < num_frames; ++i) {
            if (free_frames[i]) {
                free_frames[i] = false;
                return i;
            }
        }
        // LRU replacement
        int lruFrame = -1, minAccess = INT_MAX;
        for (auto& [segId, pt] : pageTables) {
            for (auto& page : pt.pages) {
                if (page.present && page.last_access < minAccess) {
                    minAccess = page.last_access;
                    lruFrame = page.frame_number;
                }
            }
        }
        return lruFrame;
    }

    void freeFrame(int frame) {
        if (frame >= 0 && frame < num_frames)
            free_frames[frame] = true;
    }

    double utilization() const {
        int used = std::count(free_frames.begin(), free_frames.end(), false);
        return (double)used / num_frames * 100;
    }
};

class SegmentTable {
public:
    std::vector<Segment> segments;
    std::map<int, PageTable> pageTables;
    std::map<int, DirectoryTable> directoryTables;
    PhysicalMemory physMem;
    int page_size;
    bool useLRU;

    int pageFaults = 0;
    int totalTranslations = 0;
    double totalLatency = 0;

    SegmentTable(int numFrames, int pageSize = 1000, bool lru = false)
        : physMem(numFrames), page_size(pageSize), useLRU(lru) {}

    void addSegment(int id, int base, int limit, Protection prot) {
        segments.push_back({base, limit, prot});
        pageTables[id] = PageTable(limit, page_size);
        directoryTables.emplace(id, DirectoryTable(page_size));
        directoryTables.at(id).addDirectory(0, limit);
    }

    int translateAddress(int segNum, int pageDir, int pageNum, int offset,
                         Protection accessType, int& latency) {
        latency = 1 + rand() % 5;
        totalLatency += latency;
        totalTranslations++;

        if (segNum < 0 || segNum >= (int)segments.size()) {
            std::cout << "Segmentation Fault: Invalid segment " << segNum << "\n";
            return -1;
        }

        Segment segment = segments[segNum];

        if (pageNum >= segment.limit) {
            std::cout << "Page Fault: Page " << pageNum << " exceeds limit "
                      << segment.limit << "\n";
            pageFaults++;
            return -1;
        }

        if (offset >= page_size) {
            std::cout << "Offset Fault: Offset " << offset << " exceeds page size\n";
            return -1;
        }

        if (accessType == READ_WRITE && segment.protection == READ_ONLY) {
            std::cout << "Protection Violation: Cannot write to read-only segment\n";
            return -1;
        }

        // Two-level: use directory table
        int frame = -1;
        if (directoryTables.count(segNum)) {
            frame = directoryTables.at(segNum).getFrame(pageDir, pageNum,
                                                         physMem.time++, accessType);
        } else {
            frame = pageTables[segNum].getFrameNumber(pageNum, physMem.time++, accessType);
        }

        if (frame == -1) {
            pageFaults++;
            if (useLRU)
                frame = physMem.allocateFrameLRU(pageTables);
            else
                frame = physMem.allocateFrame();

            pageTables[segNum].setFrame(pageNum, frame, accessType);
            if (directoryTables.count(segNum))
                directoryTables.at(segNum).directories[pageDir].setFrame(
                    pageNum, frame, accessType);
        }

        return segment.base_address + frame * page_size + offset;
    }

    void printMemoryMap() {
        std::cout << "\nMemory Map:\n";
        for (size_t i = 0; i < segments.size(); ++i) {
            std::cout << "Segment " << i
                      << ": Base=" << segments[i].base_address
                      << ", Limit=" << segments[i].limit
                      << ", Protection=" << (segments[i].protection == READ_ONLY ? "RO" : "RW")
                      << "\n";
            for (size_t j = 0; j < pageTables[i].pages.size(); ++j) {
                auto& p = pageTables[i].pages[j];
                std::cout << "  Page " << j
                          << ": Frame=" << p.frame_number
                          << ", Present=" << p.present
                          << ", Protection=" << (p.protection == READ_ONLY ? "RO" : "RW")
                          << "\n";
            }
        }
        std::cout << "Physical Memory Utilization: " << physMem.utilization() << "%\n";
        std::cout << "Page Faults: " << pageFaults << "\n";
        std::cout << "Avg Translation Latency: "
                  << (totalTranslations ? totalLatency / totalTranslations : 0)
                  << " units\n\n";
    }
};

void generateRandomAddresses(SegmentTable& st, int num, double validRatio,
                              const std::string& logFile) {
    std::ofstream log(logFile);
    std::mt19937 gen(std::chrono::system_clock::now().time_since_epoch().count());
    int faults = 0;

    for (int i = 0; i < num; ++i) {
        int segNum = (gen() % (st.segments.size() * 2)) * (gen() < validRatio * RAND_MAX);
        int pageNum = (gen() % (st.segments.empty() ? 10 : st.segments[0].limit * 2))
                      * (gen() < validRatio * RAND_MAX);
        int offset = gen() % (st.page_size + 100);
        int pageDir = 0;
        Protection access = (gen() % 2) ? READ_WRITE : READ_ONLY;
        int latency;

        log << "Address " << i << ": (seg=" << segNum << ", dir=" << pageDir
            << ", page=" << pageNum << ", offset=" << offset << ", "
            << (access == READ_ONLY ? "Read" : "Write") << ") ";

        int addr = st.translateAddress(segNum, pageDir, pageNum, offset, access, latency);
        if (addr == -1) {
            faults++;
            log << "Failed\n";
        } else {
            log << "Physical=" << addr << ", Latency=" << latency << "\n";
        }
    }

    log << "Page Fault Rate: " << (double)faults / num * 100 << "%\n";
    std::cout << "Results logged to " << logFile << "\n";
}

int main() {
    srand(time(0));

    int numFrames, numSegments, pageSize;
    char lruChoice;

    std::cout << "Enter number of physical frames: ";
    std::cin >> numFrames;
    std::cout << "Enter page size: ";
    std::cin >> pageSize;
    std::cout << "Enter number of segments: ";
    std::cin >> numSegments;
    std::cout << "Use LRU replacement? (y/n): ";
    std::cin >> lruChoice;

    bool useLRU = (lruChoice == 'y');

    SegmentTable segmentTable(numFrames, pageSize, useLRU);

    for (int i = 0; i < numSegments; ++i) {
        int base = i * 10000;
        int limit = 3 + rand() % 5;
        Protection prot = (rand() % 2) ? READ_ONLY : READ_WRITE;
        segmentTable.addSegment(i, base, limit, prot);
    }

    segmentTable.printMemoryMap();

    std::cout << "Enter logical address (seg, pageDir, page, offset, access[0=read,1=write])"
              << " or -1 to stop: ";
    int segNum;
    while (std::cin >> segNum) {
        if (segNum == -1) break;
        int pageDir, pageNum, offset, access;
        std::cin >> pageDir >> pageNum >> offset >> access;
        int latency;
        int physicalAddress = segmentTable.translateAddress(
            segNum, pageDir, pageNum, offset,
            access ? READ_WRITE : READ_ONLY, latency);
        if (physicalAddress != -1)
            std::cout << "Physical Address: " << physicalAddress
                      << ", Latency: " << latency << "\n";
        segmentTable.printMemoryMap();
        std::cout << "Enter next address or -1: ";
    }

    std::cout << "Generate random addresses? (y/n): ";
    char genRand;
    std::cin >> genRand;
    if (genRand == 'y') {
        generateRandomAddresses(segmentTable, 100, 0.7, "results.txt");
    }

    return 0;
}