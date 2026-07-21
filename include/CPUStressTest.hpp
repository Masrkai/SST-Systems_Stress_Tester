#pragma once

#include <mutex>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <cstdint>

#include "TimeManager.hpp"

class CPUStressTest {
private:
    static constexpr int TEST_DURATION = 30; // seconds

    // Shared atomic variables to track CPU metrics
    std::atomic<uint64_t> hashOps{0};        // Total hashing operations
    std::atomic<bool>     running{true};     // Global stop flag for the whole test

    int numCores = 0;
    std::mutex consoleMutex;

    // Guards cpuThreads + threadRunning together, since manageThreadPool()
    // mutates both and waitForCompletion() iterates cpuThreads.
    std::mutex threadPoolMutex;
    std::vector<std::thread> cpuThreads;
    std::vector<std::atomic<bool>> threadRunning; // per-thread stop flag, sized to numCores
    std::thread poolManagerThread;

    // Per-thread op counters, sized once in initialize() to numCores and
    // indexed directly by threadId (threads only ever get added at the
    // back and removed from the back, so indices stay stable).
    std::vector<std::atomic<uint64_t>> threadOps;
    std::vector<uint64_t> lastThreadOps;   // poller-side, for delta calc
    int64_t lastThreadPollMs = 0;

    // Latest thread-pool event ("thread N added/removed — ..."), surfaced
    // to the UI via getLatestEvent(). eventSeq lets a poller detect "is
    // this a new event" without string comparisons.
    std::mutex eventMutex;
    std::string lastEventText;
    uint64_t eventSeq = 0;

    TimeManager& timeManager;

    float getCurrentSystemLoad();
    void  cpuHashStressTest(int threadId);
    void  manageThreadPool();
    void  pushEvent(const std::string& text);

public:
    CPUStressTest() : timeManager(TimeManager::getInstance()) {}
    ~CPUStressTest() = default;

    void initialize();
    void start();
    void stop();
    void waitForCompletion();

    uint64_t getHashOperations() const { return hashOps.load(std::memory_order_relaxed); }
    int getCoreCount() const { return numCores; }
    bool isRunning() const { return running.load(); }

    // GUI polling interface
    std::vector<float> getThreadLoads();                          // one entry per *active* thread, 0..1
    bool getLatestEvent(std::string& outText, uint64_t& lastSeenSeq); // true if a new event since lastSeenSeq

    CPUStressTest(const CPUStressTest&) = delete;
    CPUStressTest& operator=(const CPUStressTest&) = delete;
};