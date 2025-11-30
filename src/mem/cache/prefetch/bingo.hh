#ifndef __MEM_CACHE_PREFETCH_BINGO_HH__
#define __MEM_CACHE_PREFETCH_BINGO_HH__

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "mem/cache/prefetch/queued.hh"

namespace gem5
{

struct BingoPrefetcherParams;

namespace prefetch
{

/**
 * Bingo Spatial Data Prefetcher (HPCA 2019) — simplified integration.
 *
 * This version matches classic gem5 prefetcher style (e.g., BOP):
 *  - No direct cache probe registration from the prefetcher
 *  - Training happens based on the access stream visible in
 *    calculatePrefetch()
 *
 * Use Python knobs to control what reaches calculatePrefetch():
 *  - on_miss = True     (recommended)
 *  - on_inst = False    (recommended)
 */
class Bingo : public Queued
{
  public:
    Bingo(const BingoPrefetcherParams &p);
    ~Bingo() = default;

    void calculatePrefetch(const PrefetchInfo &pfi,
                           std::vector<AddrPriority> &addresses,
                           const CacheAccessor &cache) override;

  private:
    /* ------------------------------------------------------------
     * Types
     * ------------------------------------------------------------ */
    using Event = uint8_t;
    using EventSeq = std::vector<Event>;

    struct PatternKey
    {
        EventSeq seq;

        bool operator==(const PatternKey &o) const { return seq == o.seq; }
    };

    struct PatternKeyHash
    {
        std::size_t operator()(const PatternKey &k) const
        {
            std::size_t h = 0;
            for (auto e : k.seq) {
                h = (h * 1315423911u) ^ (std::size_t(e)
                    + 0x9e3779b97f4a7c15ULL);
            }
            return h;
        }
    };

    struct PatternEntry
    {
        int nextOffset = 0;
        int confidence = 0;
        int usefulness = 0;
    };

    struct EventHistory
    {
        Addr lastRegion = 0;
        int lastOffset = 0;
        EventSeq history;

        // Simple training signal: last observed offset inside current region
        int lastSeenOffset = 0;
        bool initialized = false;
    };

    /* ------------------------------------------------------------
     * Parameters
     * ------------------------------------------------------------ */
    const unsigned regionSize;
    const unsigned lineSize;
    const unsigned historyLength;
    const unsigned numBuckets;
    const unsigned maxPatterns;
    const unsigned degree;

    /* ------------------------------------------------------------
     * Tables
     * ------------------------------------------------------------ */
    std::unordered_map<Addr, EventHistory> eventTable; // PC -> rolling events
    std::unordered_map<PatternKey, PatternEntry, PatternKeyHash> patternTable;

    /* ------------------------------------------------------------
     * Helpers
     * ------------------------------------------------------------ */
    Addr getRegion(Addr a) const { return a & ~(Addr(regionSize - 1)); }

    int getOffset(Addr a) const
    {
        return int((a & (Addr(regionSize - 1))) / lineSize);
    }

    Event computeEventBucket(int delta) const;

    void updateEventTable(Addr pc, Addr region, int offset);

    void trainPattern(const EventHistory &eh, int nextOffset);

    PatternEntry *matchPattern(const EventSeq &seq);
};

} // namespace prefetch
} // namespace gem5

#endif // __MEM_CACHE_PREFETCH_BINGO_HH__
