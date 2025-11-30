#ifndef __MEM_CACHE_PREFETCH_MLOP_HH__
#define __MEM_CACHE_PREFETCH_MLOP_HH__

#include <cstdint>
#include <unordered_map>
#include <utility>
#include <vector>

#include "mem/cache/prefetch/queued.hh"

namespace gem5
{

struct MLOPPrefetcherParams;

namespace prefetch
{

/**
 * Multi-Lookahead Offset Prefetcher (MLOP)
 *
 * Implementation style matches classic gem5 prefetchers (e.g., BOP):
 *  - No direct probe registration from the prefetcher
 *  - Learning happens using the accesses that reach calculatePrefetch()
 *
 * Configure from Python to approximate "demand-miss-only learning":
 *  - on_miss = True
 *  - prefetch_on_access = False (or True if you want every access)
 *
 * This implementation:
 *  - Tracks per-PC block address history
 *  - Scores offset × lookahead pairs every evalPeriod updates
 *  - Selects best offset per lookahead
 *  - Issues prefetches at (offset * lookahead) blocks ahead/behind
 */
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
        int offset;                   // signed offset in blocks
        std::vector<uint32_t> scores; // scores[L-1]
    };

    // Parameters
    const unsigned evalPeriod;
    const unsigned lookaheadLevels;
    const int maxOffset;
    const unsigned scoreThreshold;

    // State
    // PC -> blocks
    std::unordered_map<Addr, std::vector<Addr>> pcMissHistory;
    // offset -> entry
    std::unordered_map<int, OffsetEntry> offsetTable;
    std::vector<std::pair<unsigned, const OffsetEntry*>> bestOffsets;
    unsigned missCounter;

    // Helpers
    void resetScores();
    void scoreOffsets();
    void selectBestOffsets();
    void trimHistories();
};

} // namespace prefetch
} // namespace gem5

#endif // __MEM_CACHE_PREFETCH_MLOP_HH__
