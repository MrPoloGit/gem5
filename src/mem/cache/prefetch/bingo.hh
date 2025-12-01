/**
 * https://ieeexplore.ieee.org/stamp/stamp.jsp?tp=&arnumber=8675188
 * Describes the Bingo Spatial Data prefetcher based on HPCA 2019 paper.
 */

#ifndef __MEM_CACHE_PREFETCH_BINGO_HH__
#define __MEM_CACHE_PREFETCH_BINGO_HH__

#include <deque>
#include <map>
#include <vector>

#include "base/sat_counter.hh"
#include "mem/cache/prefetch/queued.hh"
#include "mem/packet.hh"

namespace gem5
{

struct BingoPrefetcherParams;

namespace prefetch
{

class Bingo : public Queued
{
  private:
    /** Region size in bytes (typically 4KB) */
    const unsigned regionSize;
    /** Log2 of region size */
    const unsigned regionSizeLog2;

    /**
     * Structure to hold the metadata for an active page in the
     * Accumulation Table.
     */
    struct ActiveRegionEntry
    {
        Addr pc;            // Trigger PC
        Addr offset;        // Trigger Offset
        std::vector<bool> footprint; // Bitvector of accessed blocks
        Tick lastAccess;    // For LRU within Accumulation Table

        ActiveRegionEntry(unsigned blocks_per_region)
            : pc(0), offset(0), footprint(blocks_per_region, false),
              lastAccess(0)
        {}
    };

    /**
     * Accumulation Table (Active Generation Table)
     * Maps a Page Address -> ActiveRegionEntry
     */
    std::map<Addr, ActiveRegionEntry> accumulationTable;
    unsigned accumulationTableEntries;

    /**
     * History Table Entry
     * Stores the pattern learned from a previous page residency.
     */
    struct PatternEntry
    {
        bool valid;
        Addr pc;            // Part of Long Event
        Addr address;       // Part of Long Event (Tag)
        Addr offset;        // Part of Short Event
        std::vector<bool> footprint;
        Tick lastUse;       // For LRU replacement

        PatternEntry() : valid(false), pc(0), address(0), offset(0), lastUse(0) {}
    };

    /**
     * History Table (Unified PHT)
     * Modeled as a set-associative structure.
     */
    std::vector<std::vector<PatternEntry>> historyTable;
    unsigned historyTableSize;
    unsigned historyTableAssoc;

    /**
     * Helper to determine the number of blocks in a region
     */
    unsigned blocksPerRegion;

    /**
     * Hash function for indexing the History Table using Short Event.
     * Short Event = PC + Offset
     */
    uint32_t hashShortEvent(Addr pc, Addr offset) const;

    /**
     * Normalize a generic address to a Page/Region base address.
     */
    Addr pageAddress(Addr addr) const;

    /**
     * Calculate offset of a block within its page.
     */
    Addr pageOffset(Addr addr) const;

    /**
     * Handles eviction updates (Moves from Accumulation -> History).
     */
    using EvictionInfo = CacheDataUpdateProbeArg;
    void notifyEvict(const EvictionInfo &info) override;

  public:
    Bingo(const BingoPrefetcherParams &p);
    ~Bingo() = default;

    void calculatePrefetch(const PrefetchInfo &pfi,
                           std::vector<AddrPriority> &addresses,
                           const CacheAccessor &cache) override;
};

} // namespace prefetch
} // namespace gem5

#endif // __MEM_CACHE_PREFETCH_BINGO_HH__
