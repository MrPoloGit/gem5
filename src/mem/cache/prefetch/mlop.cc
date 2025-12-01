/**
 * https://mshakerinava.github.io/papers/mlop-dpc3.pdf
 * Describes the Multi Level Offset prefetcher based on ISCA 2019 paper.
 */

#include "mem/cache/prefetch/mlop.hh"

#include "base/logging.hh"
#include "debug/HWPrefetch.hh"
#include "params/MLOPPrefetcher.hh"

namespace gem5
{

namespace prefetch
{

MLOP::MLOP(const MLOPPrefetcherParams &p)
    : Queued(p),
      evalPeriod(p.evaluation_period),
      lookaheadLevels(p.lookahead_levels),
      maxOffset(p.max_offset),
      scoreThreshold(p.score_threshold),
      missCounter(0)
{
    if (lookaheadLevels == 0) {
        fatal("%s: lookahead_levels must be > 0\n", name());
    }
    if (evalPeriod == 0) {
        fatal("%s: evaluation_period must be > 0\n", name());
    }
    if (maxOffset <= 0) {
        fatal("%s: max_offset must be > 0\n", name());
    }

    // Populate offset table: [-maxOffset, +maxOffset], excluding 0.
    for (int o = -maxOffset; o <= maxOffset; ++o) {
        if (o == 0)
            continue;

        OffsetEntry e;
        e.offset = o;
        e.scores.assign(lookaheadLevels, 0);
        offsetTable.emplace(o, std::move(e));
    }

    DPRINTF(HWPrefetch,
            "%s: init evalPeriod=%u lookaheadLevels=%u maxOffset=%d "
            "scoreThreshold=%u offsets=%zu blkSize=%u\n",
            name(), evalPeriod, lookaheadLevels, maxOffset, scoreThreshold,
            offsetTable.size(), blkSize);
}

void
MLOP::resetScores()
{
    for (auto &kv : offsetTable) {
        auto &e = kv.second;
        std::fill(e.scores.begin(), e.scores.end(), 0);
    }
}

void
MLOP::trimHistories()
{
    // Keep each PC history bounded to ~2*evalPeriod entries.
    const size_t maxLen = std::max<size_t>(2 * evalPeriod, 8);
    for (auto &kv : pcMissHistory) {
        auto &hist = kv.second;
        if (hist.size() > maxLen) {
            const size_t drop = hist.size() - maxLen;
            hist.erase(hist.begin(), hist.begin() + drop);
        }
    }
}

void
MLOP::scoreOffsets()
{
    // Recompute fresh scores for this epoch.
    resetScores();

    for (auto &kv : pcMissHistory) {
        auto &hist = kv.second;
        if (hist.size() < 2)
            continue;

        for (size_t i = 1; i < hist.size(); ++i) {
            const int delta = int(hist[i] - hist[i - 1]); // in blocks

            for (auto &ov : offsetTable) {
                const int o = ov.first;
                OffsetEntry &entry = ov.second;

                for (unsigned L = 1; L <= lookaheadLevels; ++L) {
                    if (o * int(L) == delta) {
                        entry.scores[L - 1]++;
                    }
                }
            }
        }
    }

    trimHistories();
}

void
MLOP::selectBestOffsets()
{
    bestOffsets.clear();

    for (unsigned L = 1; L <= lookaheadLevels; ++L) {
        uint32_t bestScore = 0;
        const OffsetEntry *bestEntry = nullptr;

        for (auto &kv : offsetTable) {
            const OffsetEntry &e = kv.second;
            const uint32_t s = e.scores[L - 1];

            if (s >= scoreThreshold && s > bestScore) {
                bestScore = s;
                bestEntry = &e;
            }
        }

        if (bestEntry) {
            bestOffsets.emplace_back(L, bestEntry);
        }
    }
}

void
MLOP::calculatePrefetch(const PrefetchInfo &pfi,
                        std::vector<AddrPriority> &addresses,
                        const CacheAccessor &cache)
{
    const Addr pc = pfi.getPC();
    if (pc == 0)
        return;

    const Addr addr = pfi.getAddr();

    // Work in cache-line blocks using Queued's lBlkSize.
    const Addr block = addr >> lBlkSize;

    // Record this access as part of the stream for this PC.
    pcMissHistory[pc].push_back(block);
    missCounter++;

    if (missCounter >= evalPeriod) {
        scoreOffsets();
        selectBestOffsets();
        missCounter = 0;

        DPRINTF(HWPrefetch, "%s: epoch scoring done, bestOffsets=%zu\n",
                name(), bestOffsets.size());
    }

    if (bestOffsets.empty())
        return;

    // Issue prefetches. Use (offset * lookahead) blocks to
    // respect multi-lookahead.
    for (const auto &p : bestOffsets) {
        const unsigned L = p.first;
        const OffsetEntry *e = p.second;

        const int o = e->offset;
        const int dist = o * int(L);

        const Addr pf_addr = addr + (Addr(dist) << lBlkSize);
        addresses.emplace_back(pf_addr, 0);

        DPRINTF(HWPrefetch, "%s: prefetch %#lx pc=%#lx o=%d L=%u dist=%d\n",
                name(), pf_addr, pc, o, L, dist);
    }
}

} // namespace prefetch
} // namespace gem5
