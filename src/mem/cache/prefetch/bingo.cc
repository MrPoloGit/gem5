#include "mem/cache/prefetch/bingo.hh"

#include "base/logging.hh"
#include "debug/HWPrefetch.hh"
#include "params/BingoPrefetcher.hh"

namespace gem5
{

namespace prefetch
{

/* ================================================================
 * Constructor
 * ================================================================ */
Bingo::Bingo(const BingoPrefetcherParams &p)
    : Queued(p),
      regionSize(p.region_size),
      lineSize(p.block_size),
      historyLength(p.event_history_len),
      numBuckets(p.bucket_count),
      maxPatterns(p.pattern_table_entries),
      degree(p.prefetch_degree)
{
    // Reserve space to avoid rehashing
    patternTable.reserve(maxPatterns);
}


/* ================================================================
 * Compute event bucket (offset difference → coarse bucket)
 * ================================================================ */
Bingo::Event
Bingo::computeEventBucket(int delta) const
{
    // TODO: Fill in bucket thresholds based on Bingo paper
    // Example skeleton:
    if (delta == 0) return 0;
    if (delta == 1) return 1;
    if (delta == -1) return 2;
    if (delta > 1 && delta < 4) return 3;
    if (delta < -1 && delta > -4) return 4;
    return 15; // "large jump"
}


/* ================================================================
 * Update Event Table for a PC
 * ================================================================ */
void
Bingo::updateEventTable(Addr pc, Addr region, int offset)
{
    auto &eh = eventTable[pc];

    // Detect region change
    if (eh.history.empty() || eh.lastRegion != region) {
        eh.history.assign(historyLength, 0);
        eh.lastRegion = region;
        eh.lastOffset = offset;
        return;
    }

    // Compute bucket
    int delta = offset - eh.lastOffset;
    Event e = computeEventBucket(delta);
    eh.lastOffset = offset;

    // Shift event sequence
    eh.history.erase(eh.history.begin());
    eh.history.push_back(e);
}


/* ================================================================
 * Pattern Training (called on eviction or region end)
 * ================================================================ */
void
Bingo::trainPattern(const EventHistory &eh, int nextOffset)
{
    if (patternTable.size() >= maxPatterns) {
        // TODO: real replacement policy (LRU, usefulness)
        patternTable.erase(patternTable.begin());
    }

    PatternKey k{eh.history};
    patternTable[k] = PatternEntry{nextOffset, 1, 0};
}


/* ================================================================
 * Pattern Matching
 * ================================================================ */
Bingo::PatternEntry*
Bingo::matchPattern(const EventSeq &seq)
{
    PatternKey k{seq};
    auto it = patternTable.find(k);
    if (it == patternTable.end())
        return nullptr;
    return &it->second;
}


/* ================================================================
 * Prefetching action: main engine
 * ================================================================ */
void
Bingo::calculatePrefetch(const PrefetchInfo &pfi,
                         std::vector<AddrPriority> &addresses,
                         const CacheAccessor &cache)
{
    Addr addr = pfi.getAddr();
    Addr pc   = pfi.getPC();

    if (pc == 0)
        return;

    Addr region = getRegion(addr);
    int offset  = getOffset(addr);

    // Update ET for this PC
    updateEventTable(pc, region, offset);

    auto &eh = eventTable[pc];

    // Try to match pattern
    PatternEntry *pe = matchPattern(eh.history);
    if (!pe)
        return;

    // Produce prefetches (spatial offsets)
    for (int i = 1; i <= degree; i++) {
        int targetOffset = pe->nextOffset + (i - 1);
        if (targetOffset < 0) continue;

        Addr pf_addr = region + targetOffset * lineSize;

        addresses.push_back(AddrPriority(pf_addr, 0));
        DPRINTF(HWPrefetch, "Bingo: prefetch %#lx (PC %#lx)\n", pf_addr, pc);
    }
}


/* ================================================================
 * Region eviction — learning phase trigger
 * ================================================================ */
void
Bingo::notifyEvict(const EvictionInfo &info)
{
    Addr pc = info.pfi.getPC();
    if (pc == 0) return;

    auto it = eventTable.find(pc);
    if (it == eventTable.end())
        return;

    EventHistory &eh = it->second;

    int evictedOffset = getOffset(info.addr);

    // Train pattern: (event sequence → future offset)
    trainPattern(eh, evictedOffset);
}

} // namespace prefetch
} // namespace gem5
