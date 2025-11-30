// Copyright (...)
#ifndef __MEM_CACHE_PREFETCH_BINGO_HH__
#define __MEM_CACHE_PREFETCH_BINGO_HH__

#include <deque>
#include <unordered_map>
#include <vector>

#include "mem/cache/prefetch/queued.hh"
#include "params/BingoPrefetcher.hh"

namespace gem5
{

namespace prefetch
{

/**
 * Bingo Spatial Data Prefetcher (HPCA 2019).
 * Uses event histories and pattern table matching to predict future offsets.
 */
class Bingo : public Queued
{
  public:
    /** Bingo constructor (parameters come from BingoPrefetcher.py) */
    Bingo(const BingoPrefetcherParams &p);

    /** Called on each demand access */
    void calculatePrefetch(const PrefetchInfo &pfi,
                           std::vector<AddrPriority> &addresses,
                           const CacheAccessor &cache) override;

    /** Called when a cache line is evicted */
    void notifyEvict(const EvictionInfo &info) override;

  private:
    /* ------------------------------------------------------------
     * Types + Data Structures
     * ------------------------------------------------------------ */

    /** Compact event bucket ID */
    using Event = uint8_t;

    /** Fixed-length sequence of events */
    using EventSeq = std::vector<Event>;

    /** Key for Pattern Table: hashed event sequence */
    struct PatternKey
    {
        EventSeq seq;

        bool operator==(const PatternKey &other) const {
            return seq == other.seq;
        }
    };

    /** Hash function for PatternKey */
    struct PatternKeyHash
    {
        std::size_t operator()(const PatternKey &k) const {
            std::size_t h = 0;
            for (auto e : k.seq)
                h = (h * 1315423911u) ^ (e + 0x9e3779b97f4a7c15ULL);
            return h;
        }
    };

    /** Pattern Table Entry */
    struct PatternEntry
    {
        int nextOffset;     // offset predicted by this pattern
        int confidence;     // incremented on correct predictions
        int usefulness;     // optional for replacement policy
    };

    /** Event Table Entry */
    struct EventHistory
    {
        Addr lastRegion;           // region tag (aligned)
        int lastOffset;            // last observed normalized offset
        EventSeq history;          // rolling event-history
    };

    /* ------------------------------------------------------------
     * Prefetcher Parameters
     * ------------------------------------------------------------ */
    const unsigned regionSize;       // bytes per region
    const unsigned lineSize;         // block size
    const unsigned historyLength;    // event sequence length
    const unsigned numBuckets;       // bucket count
    const unsigned maxPatterns;      // pattern table capacity
    const unsigned degree;           // prefetch degree

    /* ------------------------------------------------------------
     * Bingo Tables
     * ------------------------------------------------------------ */

    /** PC-indexed event history table */
    std::unordered_map<Addr, EventHistory> eventTable;

    /** Pattern Table */
    std::unordered_map<PatternKey, PatternEntry, PatternKeyHash> patternTable;

    /* ------------------------------------------------------------
     * Helper Functions
     * ------------------------------------------------------------ */

    /** Compute region base */
    Addr getRegion(Addr a) const {
        return a & ~(regionSize - 1);
    }

    /** Convert address to region-local offset */
    int getOffset(Addr a) const {
        return (a & (regionSize - 1)) / lineSize;
    }

    /** Bucketization of offset difference → event */
    Event computeEventBucket(int delta) const;

    /** Insert a pattern into Pattern Table */
    void addPattern(const EventSeq &seq, int nextOff);

    /** Match event sequence in Pattern Table */
    PatternEntry* matchPattern(const EventSeq &seq);

    /** Update event table for a PC */
    void updateEventTable(Addr pc, Addr region, int offset);

    /** Train pattern on region eviction */
    void trainPattern(const EventHistory &eh, int nextOffset);
};

} // namespace prefetch
} // namespace gem5

#endif // __MEM_CACHE_PREFETCH_BINGO_HH__
