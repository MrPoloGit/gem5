// Copyright (...)
#ifndef __MEM_CACHE_PREFETCH_MLOP_HH__
#define __MEM_CACHE_PREFETCH_MLOP_HH__

#include <unordered_map>
#include <vector>

#include "mem/cache/prefetch/queued.hh"
#include "params/MLOPPrefetcher.hh"

namespace gem5
{

namespace prefetch
{

/**
 * Multi-Lookahead Offset Prefetcher (MLOP)
 * Based on the DPC3 design:
 *   - Tracks deltas between consecutive misses (per PC)
 *   - Scores offsets × lookahead levels
 *   - Selects highest-scoring offsets
 *   - Issues prefetches through Queued prefetcher engine
 */
class MLOP : public Queued
{
  public:
    /** Constructor: parameters come from MLOPPrefetcher.py */
    MLOP(const MLOPPrefetcherParams &p);

    /** Called on each memory access notification */
    void notify(const PrefetchInfo &pfi) override;

    /** Called to generate prefetches for a given access */
    void calculatePrefetch(const PrefetchInfo &pfi,
                           std::vector<AddrPriority> &addresses,
                           const CacheAccessor &cache) override;

  private:
    /* ------------------------------------------------------------
     * Types + Tables
     * ------------------------------------------------------------ */

    /** Per-offset entry storing scores per lookahead level */
    struct OffsetEntry
    {
        int offset;                       // signed offset in cache lines
        std::vector<uint32_t> scores;     // one score per lookahead level
    };

    /* ------------------------------------------------------------
     * Prefetcher Parameters (from Python SimObject)
     * ------------------------------------------------------------ */
    const unsigned evalPeriod;         // demand misses between evaluations
    const unsigned lookaheadLevels;    // number of lookahead depths
    const int      maxOffset;          // maximum absolute offset tested
    const unsigned scoreThreshold;     // minimal score to consider offset
    const unsigned lineSize;           // bytes per cache line

    /* ------------------------------------------------------------
     * MLOP Internal State
     * ------------------------------------------------------------ */

    /** Miss history per PC (PC → vector of block numbers) */
    std::unordered_map<Addr, std::vector<Addr>> pcMissHistory;

    /** Offset table: offset → entry */
    std::unordered_map<int, OffsetEntry> offsetTable;

    /** Best offsets selected for this scoring round */
    std::vector<std::pair<unsigned, const OffsetEntry*>> bestOffsets;

    /** Counter for periodic scoring */
    unsigned missCounter;

    /* ------------------------------------------------------------
     * Core MLOP Scoring Logic
     * ------------------------------------------------------------ */

    /** Score offsets × lookahead levels based on recorded deltas */
    void scoreOffsets();

    /** Select highest-scoring offsets for use in calculatePrefetch() */
    void selectBestOffsets();
};

} // namespace prefetch
} // namespace gem5

#endif // __MEM_CACHE_PREFETCH_MLOP_HH__
