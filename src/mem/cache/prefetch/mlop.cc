#include "mem/cache/prefetch/mlop.hh"

#include "base/logging.hh"
#include "debug/HWPrefetch.hh"
#include "params/MLOPPrefetcher.hh"

namespace gem5
{

namespace prefetch
{

/* ================================================================
 * Constructor
 * ================================================================ */
MLOP::MLOP(const MLOPPrefetcherParams &p)
    : Queued(p),
      evalPeriod(p.evaluation_period),
      lookaheadLevels(p.lookahead_levels),
      maxOffset(p.max_offset),
      scoreThreshold(p.score_threshold),
      lineSize(p.block_size),
      missCounter(0)
{
    // Initialize offset table for offsets [-maxOffset, maxOffset], excluding 0
    for (int o = -maxOffset; o <= maxOffset; o++) {
        if (o == 0) continue;

        OffsetEntry e;
        e.offset = o;
        e.scores.assign(lookaheadLevels, 0);
        offsetTable[o] = e;
    }

    DPRINTF(HWPrefetch,
        "MLOP: Initialized with %d offsets and %d lookahead levels\n",
        (int)offsetTable.size(), lookaheadLevels);
}


/* ================================================================
 * Record demand misses for scoring
 * ================================================================ */
void
MLOP::notify(const PrefetchInfo &pfi)
{
    Addr addr = pfi.getAddr();
    Addr pc   = pfi.getPC();

    if (pc == 0)
        return;

    // Use cache-line–aligned block address
    Addr block = addr / lineSize;

    // Add to miss history for this PC
    pcMissHistory[pc].push_back(block);
    missCounter++;

    // Periodic scoring
    if (missCounter >= evalPeriod) {
        scoreOffsets();
        missCounter = 0;
    }
}


/* ================================================================
 * Scoring logic:
 * For each PC, examine sequences of deltas and match them to
 * offset × lookahead values.
 * ================================================================ */
void
MLOP::scoreOffsets()
{
    for (auto &kv : pcMissHistory) {
        auto &hist = kv.second;
        if (hist.size() < 2)
            continue;

        // Scan consecutive deltas
        for (size_t i = 1; i < hist.size(); i++) {
            int delta = int(hist[i] - hist[i - 1]);

            // Compare delta against offset × lookahead
            for (auto &ov : offsetTable) {
                int o = ov.first;
                OffsetEntry &entry = ov.second;

                for (unsigned L = 1; L <= lookaheadLevels; L++) {
                    if (o * (int)L == delta) {
                        entry.scores[L - 1]++;
                    }
                }
            }
        }

        // Prevent unbounded growth
        if (hist.size() > evalPeriod * 2) {
            hist.erase(hist.begin(), hist.begin() + hist.size() / 2);
        }
    }

    DPRINTF(HWPrefetch, "MLOP: Scoring complete\n");
}


/* ================================================================
 * Select best-scoring offsets per lookahead
 * ================================================================ */
void
MLOP::selectBestOffsets()
{
    bestOffsets.clear();

    for (unsigned L = 1; L <= lookaheadLevels; L++) {

        uint32_t bestScore = 0;
        const OffsetEntry *bestEntry = nullptr;

        for (auto &kv : offsetTable) {
            const OffsetEntry &e = kv.second;
            uint32_t s = e.scores[L - 1];

            if (s >= scoreThreshold && s > bestScore) {
                bestScore = s;
                bestEntry = &e;
            }
        }

        if (bestEntry)
            bestOffsets.emplace_back(L, bestEntry);
    }
}


/* ================================================================
 * Prefetch generation
 * ================================================================ */
void
MLOP::calculatePrefetch(const PrefetchInfo &pfi,
                        std::vector<AddrPriority> &addresses,
                        const CacheAccessor &cache)
{
    Addr addr = pfi.getAddr();
    Addr pc   = pfi.getPC();

    if (pc == 0)
        return;

    // On each calculatePrefetch() call, ensure best offsets are ready
    selectBestOffsets();

    if (bestOffsets.empty())
        return;

    Addr block   = addr / lineSize;

    for (auto &p : bestOffsets) {
        unsigned L = p.first;
        const OffsetEntry *e = p.second;
        int o = e->offset;

        Addr pf_block = block + o;
        Addr pf_addr  = pf_block * lineSize;

        addresses.push_back(AddrPriority(pf_addr, 0));

        DPRINTF(HWPrefetch,
            "MLOP: prefetch %#lx (PC %#lx) offset=%d L=%d\n",
            pf_addr, pc, o, L);
    }
}

} // namespace prefetch
} // namespace gem5
