#include "../include/CPUStressTest.hpp"
#include "../include/ConsoleColors.hpp"

#include <chrono>
#include <iostream>
#include <algorithm>
#include <cassert>

float CPUStressTest::getCurrentSystemLoad() {
    static uint64_t lastOps = 0;
    static int64_t lastCheck = 0;

    int64_t currentTime = timeManager.getElapsedMilliseconds();
    int64_t duration = currentTime - lastCheck;

    if (duration == 0) return 0.5f;
    uint64_t currentOps = hashOps.load(std::memory_order_relaxed);
    float opsRate = static_cast<float>(currentOps - lastOps) / duration;

    lastOps = currentOps;
    lastCheck = currentTime;

    return std::min(1.0f, std::max(0.0f, opsRate / 1000.0f));
}

void CPUStressTest::cpuHashStressTest(int threadId) {
    constexpr int BATCH_SIZE = 4500;
    constexpr int CHUNK_SIZE = 1;

    auto computeIntensiveHash = [](uint64_t base, uint64_t exponent, uint64_t mod) -> uint64_t {
        uint64_t result = 1;
        uint64_t nestedFactor = 1;

        for (uint64_t i = 0; i < exponent; ++i) {
            result = (result * base) % mod;
            nestedFactor = (nestedFactor * result) % mod;

            for (uint64_t j = 0; j < exponent; ++j) {
                nestedFactor += i + j;
                result *= nestedFactor;
            }

            if (i % 10 == 0) {
                result = (result + nestedFactor) % mod;
            }
        }
        return result;
    };

    uint64_t localHashOps = 0;

    auto stillRunning = [&]() {
        return running.load(std::memory_order_relaxed)
            && threadRunning[threadId].load(std::memory_order_relaxed)
            && timeManager.shouldContinue(TEST_DURATION);
    };

    while (stillRunning()) {
        volatile uint64_t hashValue = 0;

        for (int i = 0; i < BATCH_SIZE && stillRunning(); ++i) {
            volatile uint64_t randomBase = threadId * 123456789 + i * 987654321;
            volatile uint64_t randomExponent = ((i % 2000) + 500) * (threadId % 10 + 1);
            volatile uint64_t randomModulus = 1e9 + 12347;

            hashValue = computeIntensiveHash(randomBase, randomExponent, randomModulus);

            if (hashValue % 1024 == 0) {
                hashValue = (hashValue + threadId) * (randomBase % 7);
            }

            ++localHashOps;

            if (localHashOps % CHUNK_SIZE == 0) {
                hashOps.fetch_add(CHUNK_SIZE, std::memory_order_relaxed);
                threadOps[threadId].fetch_add(CHUNK_SIZE, std::memory_order_relaxed);
                localHashOps = 0;
            }
        }

        if (localHashOps > 0) {
            hashOps.fetch_add(localHashOps, std::memory_order_relaxed);
            threadOps[threadId].fetch_add(localHashOps, std::memory_order_relaxed);
            localHashOps = 0;
        }
    }
}

void CPUStressTest::pushEvent(const std::string& text) {
    std::lock_guard<std::mutex> lock(eventMutex);
    lastEventText = text;
    ++eventSeq;
}

bool CPUStressTest::getLatestEvent(std::string& outText, uint64_t& lastSeenSeq) {
    std::lock_guard<std::mutex> lock(eventMutex);
    if (eventSeq > lastSeenSeq) {
        outText = lastEventText;
        lastSeenSeq = eventSeq;
        return true;
    }
    return false;
}

void CPUStressTest::manageThreadPool() {
    while (running && timeManager.shouldContinue(TEST_DURATION)) {
        float systemLoad = getCurrentSystemLoad();

        std::lock_guard<std::mutex> lock(threadPoolMutex);

        if (systemLoad > 0.75f && cpuThreads.size() < static_cast<size_t>(numCores)) {
            int newId = static_cast<int>(cpuThreads.size());
            threadRunning[newId].store(true, std::memory_order_relaxed);
            cpuThreads.emplace_back(&CPUStressTest::cpuHashStressTest, this, newId);
            pushEvent("thread " + std::to_string(newId) + " added — load rising");
        }
        else if (systemLoad < 0.25f && cpuThreads.size() > 1) {
            size_t lastIdx = cpuThreads.size() - 1;
            // Signal that specific worker to stop before joining it —
            // without this the join blocks forever, since the worker's
            // loop otherwise only checks the global `running` flag.
            threadRunning[lastIdx].store(false, std::memory_order_relaxed);
            if (cpuThreads[lastIdx].joinable()) {
                cpuThreads[lastIdx].join();
            }
            cpuThreads.pop_back();
            pushEvent("thread " + std::to_string(lastIdx) + " removed — load falling");
        }

        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
}

void CPUStressTest::initialize() {
    numCores = std::thread::hardware_concurrency();
    assert(numCores > 0 && "Failed to detect CPU cores");

    hashOps.store(0);
    running.store(true);

    threadOps = std::vector<std::atomic<uint64_t>>(numCores);
    for (auto& c : threadOps) c.store(0, std::memory_order_relaxed);

    threadRunning = std::vector<std::atomic<bool>>(numCores);
    for (auto& f : threadRunning) f.store(true, std::memory_order_relaxed);

    lastThreadOps.assign(numCores, 0);
    lastThreadPollMs = 0;

    eventSeq = 0;
    lastEventText.clear();
}

void CPUStressTest::start() {
    // Start with a single worker; manageThreadPool grows/shrinks the pool
    // from here based on measured load, up to numCores.
    cpuThreads.emplace_back(&CPUStressTest::cpuHashStressTest, this, 0);
    poolManagerThread = std::thread(&CPUStressTest::manageThreadPool, this);
}

void CPUStressTest::stop() {
    running = false;
}

void CPUStressTest::waitForCompletion() {
    if (poolManagerThread.joinable()) {
        poolManagerThread.join(); // exits once running == false
    }

    std::lock_guard<std::mutex> lock(threadPoolMutex);
    for (auto& thread : cpuThreads) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    cpuThreads.clear();
}

std::vector<float> CPUStressTest::getThreadLoads() {
    std::lock_guard<std::mutex> lock(threadPoolMutex);
    size_t activeCount = cpuThreads.size();

    int64_t now = timeManager.getElapsedMilliseconds();
    int64_t duration = now - lastThreadPollMs;
    lastThreadPollMs = now;

    std::vector<float> loads(activeCount, 0.0f);
    if (lastThreadOps.size() < activeCount) {
        lastThreadOps.resize(activeCount, 0);
    }
    if (duration <= 0) return loads;

    for (size_t i = 0; i < activeCount; ++i) {
        uint64_t current = threadOps[i].load(std::memory_order_relaxed);
        uint64_t delta = current - lastThreadOps[i];
        lastThreadOps[i] = current;

        float rate = static_cast<float>(delta) / duration;
        loads[i] = std::min(1.0f, std::max(0.0f, rate / 1000.0f));
    }
    return loads;
}