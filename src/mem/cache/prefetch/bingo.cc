#include "mem/cache/prefetch/bingo.hh"

#include "base/logging.hh"
#include "debug/HWPrefetch.hh"
#include "params/BingoPrefetcher.hh"

namespace gem5
{

namespace prefetch
{

Bingo::Bingo(const BingoPrefetcherParams &p)
    : Queued(p),
      regionSize(p.region_size),
      lineSize(p.block_size),
      historyLength(p.event_history_len),
      numBuckets(p.bucket_count),
      maxPatterns(p.pattern_table_entries),
      degree(p.prefetch_degree)
{
    if (!isPowerOf2(regionSize)) {
        fatal("%s: region_size must be power of 2\n", name());
    }
    if (!isPowerOf2(lineSize)) {
        fatal("%s: block_size must be power of 2\n", name());
    }
    if (lineSize > regionSize) {
        fatal("%s: block_size must be <= region_size\n", name());
    }
    if (historyLength == 0) {
        fatal("%s: event_history_len must be > 0\n", name());
    }
    if (degree == 0) {
        fatal("%s: prefetch_degree must be > 0\n", name());
    }

    patternTable.reserve(maxPatterns);

    DPRINTF(HWPrefetch,
            "%s: init regionSize=%u lineSize=%u historyLength=%u buckets=%u "
            "maxPatterns=%u degree=%u\n",
            name(), regionSize, lineSize, historyLength, numBuckets,
            maxPatterns, degree);
}

Bingo::Event
Bingo::computeEventBucket(int delta) const
{
    // Placeholder bucketization (you can refine to match paper)
    if (delta == 0)  return 0;
    if (delta == 1)  return 1;
    if (delta == -1) return 2;
    if (delta > 1 && delta < 4)   return 3;
    if (delta < -1 && delta > -4) return 4;
    return 15;
}

void
Bingo::updateEventTable(Addr pc, Addr region, int offset)
{
    auto &eh = eventTable[pc];

    if (!eh.initialized || eh.history.empty() || eh.lastRegion != region) {
        eh.history.assign(historyLength, 0);
        eh.lastRegion = region;
        eh.lastOffset = offset;
        eh.lastSeenOffset = offset;
        eh.initialized = true;
        return;
    }

    const int delta = offset - eh.lastOffset;
    const Event e = computeEventBucket(delta);
    eh.lastOffset = offset;
    eh.lastSeenOffset = offset;

    // shift left by one
    eh.history.erase(eh.history.begin());
    eh.history.push_back(e);
}

void
Bingo::trainPattern(const EventHistory &eh, int nextOffset)
{
    if (maxPatterns != 0 && patternTable.size() >= maxPatterns) {
        // Simple eviction policy: erase arbitrary entry (OK to start)
        patternTable.erase(patternTable.begin());
    }

    PatternKey key{eh.history};
    patternTable[key] = PatternEntry{nextOffset, 1, 0};
}

Bingo::PatternEntry *
Bingo::matchPattern(const EventSeq &seq)
{
    PatternKey key{seq};
    auto it = patternTable.find(key);
    return (it == patternTable.end()) ? nullptr : &it->second;
}

void
Bingo::calculatePrefetch(const PrefetchInfo &pfi,
                         std::vector<AddrPriority> &addresses,
                         const CacheAccessor &cache)
{
    const Addr pc = pfi.getPC();
    if (pc == 0)
        return;

    const Addr addr = pfi.getAddr();
    const Addr region = getRegion(addr);
    const int offset = getOffset(addr);

    // Train on region transitions (a stand-in for true “evict/region-end”)
    auto it = eventTable.find(pc);
    if (it != eventTable.end()) {
        auto &eh_prev = it->second;
        if (eh_prev.initialized && eh_prev.lastRegion != region) {
            // When we leave a region, store the last seen offset as
            // "nextOffset"
            trainPattern(eh_prev, eh_prev.lastSeenOffset);
        }
    }

    // Update rolling event stream
    updateEventTable(pc, region, offset);

    // Attempt match
    auto &eh = eventTable[pc];
    PatternEntry *pe = matchPattern(eh.history);
    if (!pe)
        return;

    // Generate spatial prefetches starting at predicted nextOffset
    for (unsigned i = 0; i < degree; ++i) {
        const int target = pe->nextOffset + int(i);
        if (target < 0)
            continue;

        const Addr pf_addr = region + Addr(target * int(lineSize));
        addresses.emplace_back(pf_addr, 0);

        DPRINTF(HWPrefetch, "%s: prefetch %#lx pc=%#lx\n",
                name(), pf_addr, pc);
    }
}

} // namespace prefetch
} // namespace gem5
