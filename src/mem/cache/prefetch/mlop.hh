/**
 * https://mshakerinava.github.io/papers/mlop-dpc3.pdf
 * Multi-Lookahead Offset Prefetching (MLOP)
 */

#ifndef __MEM_CACHE_PREFETCH_MLOP_HH__
#define __MEM_CACHE_PREFETCH_MLOP_HH__

#include <cstdint>
#include <unordered_map>
#include <utility>
#include <vector>

#include "base/types.hh"
#include "mem/cache/prefetch/queued.hh"

namespace gem5
{

struct MLOPPrefetcherParams;

namespace prefetch
{

class MLOP : public Queued
{
  public:
    MLOP(const MLOPPrefetcherParams &p);
    ~MLOP() = default;

    void calculatePrefetch(const PrefetchInfo &pfi,
                           std::vector<AddrPriority> &addresses,
                           const CacheAccessor &cache) override;

  private:
    struct OffsetEntry
    {
        int offset;        // signed offset in blocks (cache lines)
        std::vector<uint32_t> scores; // scores[L-1],
    };

    struct AMTEntry
    {
        bool valid = false;
        Addr base_block = 0;
        uint64_t bv = 0;

        // Most-recent-first indices [0..bitVectorSize-1]
        // , size <= recentDepth
        std::vector<uint8_t> recent;

        uint64_t last_touch = 0; // LRU replacement
    };

    // Parameters (runtime-configurable in Prefetcher.py)
    const unsigned evalPeriod;
    const unsigned lookaheadLevels;
    const int maxOffset;
    const unsigned scoreThreshold;
    const unsigned prefetchDegree;
    const unsigned amtEntries;
    const unsigned bitVectorSize;
    const unsigned recentDepth;
    const Addr     regionMask;

    // State
    std::vector<AMTEntry> amt;
    uint64_t amtClock = 0;

    // offset -> scores
    std::unordered_map<int, OffsetEntry> offsetTable;
    std::vector<std::pair<unsigned, const OffsetEntry*>> bestOffsets;

    unsigned accessCounter = 0;

    // Helpers
    void resetScores();
    void selectBestOffsets();

    AMTEntry &findOrAllocAmtEntry(Addr base_block);
    void updateScoresWithAccess(Addr block);

    static inline unsigned ctz64(uint64_t x)
    {
        return (unsigned)__builtin_ctzll(x);
    }

    static inline bool isPowerOfTwo(unsigned x)
    {
        return x && ((x & (x - 1)) == 0);
    }
};

} // namespace prefetch
} // namespace gem5

#endif // __MEM_CACHE_PREFETCH_MLOP_HH__
