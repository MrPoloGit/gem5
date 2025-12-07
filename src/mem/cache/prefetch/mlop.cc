/**
* https://mshakerinava.github.io/papers/mlop-dpc3.pdf
* Multi-Lookahead Offset Prefetching (MLOP)
*/

#include "mem/cache/prefetch/mlop.hh"

#include <algorithm>
#include <cassert>

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
      prefetchDegree(p.prefetch_degree),
      amtEntries(p.amt_entries),
      bitVectorSize(p.bit_vector_size),
      recentDepth(lookaheadLevels > 0 ? lookaheadLevels - 1 : 0),
      regionMask(Addr(bitVectorSize - 1)),
      amt(amtEntries)
{
    if (lookaheadLevels == 0) {
        fatal("%s: lookahead_levels must be > 0\n", name());
    }

    if (!isPowerOfTwo(bitVectorSize)) {
        fatal("%s: bit_vector_size (%u) must be a power of two (e.g., 64)\n",
              name(), bitVectorSize);
    }

    if (maxOffset <= 0) {
        fatal("%s: max_offset must be > 0 (got %d)\n", name(), maxOffset);
    }

    if (maxOffset >= int(bitVectorSize)) {
        fatal("%s: max_offset (%d) must be < bit_vector_size (%u)\n",
              name(), maxOffset, bitVectorSize);
    }

    if (scoreThreshold == 0) {
        fatal("%s: score_threshold must be > 0\n", name());
    }

    if (recentDepth > 0 && recentDepth >= bitVectorSize) {
        fatal("%s: lookahead_levels (%u) implies recentDepth=%u, which must "
              "be < bit_vector_size (%u)\n",
              name(), lookaheadLevels, recentDepth, bitVectorSize);
    }

    // Populate offset table
    // from -maxOffset to +maxOffset, excluding 0.
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
            "scoreThreshold=%u prefetchDegree=%u offsets=%zu blkSize=%u "
            "AMT=%u bitVectorSize=%u\n",
            name(), evalPeriod, lookaheadLevels, maxOffset, scoreThreshold,
            prefetchDegree, offsetTable.size(), blkSize,
            (unsigned)amt.size(), bitVectorSize);
}

void
MLOP::resetScores()
{
    for (auto &kv : offsetTable) {
        std::fill(kv.second.scores.begin(), kv.second.scores.end(), 0);
    }
}

MLOP::AMTEntry &
MLOP::findOrAllocAmtEntry(Addr base_block)
{
    int free_i = -1;
    int lru_i = -1;
    uint64_t lru_touch = UINT64_MAX;

    for (unsigned i = 0; i < amt.size(); ++i) {
        AMTEntry &e = amt[i];
        if (e.valid && e.base_block == base_block) {
            e.last_touch = ++amtClock;
            return e;
        }
        if (!e.valid && free_i < 0)
            free_i = int(i);
        if (e.valid && e.last_touch < lru_touch) {
            lru_touch = e.last_touch;
            lru_i = int(i);
        }
    }

    const int victim = (free_i >= 0) ? free_i : lru_i;
    assert(victim >= 0);

    AMTEntry &v = amt[victim];
    v.valid = true;
    v.base_block = base_block;
    v.bv = 0;
    v.recent.clear();
    v.recent.reserve(recentDepth);
    v.last_touch = ++amtClock;
    return v;
}

void
MLOP::updateScoresWithAccess(Addr block)
{
    // cache-line block address
    const Addr base_block = block & ~regionMask;
    const unsigned idx = unsigned(block & regionMask);

    AMTEntry &entry = findOrAllocAmtEntry(base_block);

    // 1) Start from the current bit-vector.
    // 2) Exclude last (L-1) accesses by clearing their bits.
    // 3) For each set bit j, offset k = idx - j gets +1 at level L.
    for (unsigned L = 1; L <= lookaheadLevels; ++L) {
        uint64_t masked = entry.bv;

        const unsigned exclude = (L > 1) ? (L - 1) : 0;
        const unsigned ex = std::min<unsigned>(exclude,
                                               (unsigned)entry.recent.size());
        for (unsigned r = 0; r < ex; ++r) {
            masked &= ~(1ULL << entry.recent[r]);
        }

        uint64_t bits = masked;
        while (bits) {
            const unsigned j = ctz64(bits);
            bits &= (bits - 1); // clear lowest set bit

            const int k = int(idx) - int(j);   // signed offset in lines
            if (k == 0)
                continue;
            if (k < -maxOffset || k > maxOffset)
                continue;

            auto it = offsetTable.find(k);
            if (it != offsetTable.end()) {
                it->second.scores[L - 1]++;
            }
        }
    }

    // Update ordered recent list (most-recent-first),
    // max length = recentDepth.
    if (recentDepth > 0) {
        entry.recent.insert(entry.recent.begin(), uint8_t(idx));
        if (entry.recent.size() > recentDepth) {
            entry.recent.resize(recentDepth);
        }
    } else {
        entry.recent.clear();
    }

    // Set the bit for this access
    entry.bv |= (1ULL << idx);
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
    const Addr addr = pfi.getAddr();
    const Addr pc = pfi.hasPC() ? pfi.getPC() : 0;

    const Addr block = addr >> lBlkSize;

    // Train on every access
    updateScoresWithAccess(block);
    accessCounter++;

    if (accessCounter >= evalPeriod) {
        selectBestOffsets();
        resetScores();
        accessCounter = 0;

        DPRINTF(HWPrefetch, "%s: epoch done, bestOffsets=%zu\n",
                name(), bestOffsets.size());
    }

    if (bestOffsets.empty())
        return;

    // Issue prefetches in increasing lookahead order (L=1,2,...),
    // using priority so that L=1 is issued earliest by Queued.
    unsigned issued = 0;

    for (const auto &p : bestOffsets) {
        if (issued >= prefetchDegree)
            break;

        const unsigned L = p.first;
        const OffsetEntry *e = p.second;

        const int o = e->offset;
        const Addr pf_addr = addr + (Addr(o) << lBlkSize);

        // Check page boundaries using
        if (!samePage(addr, pf_addr))
            continue;

        // const int32_t prio = int32_t(lookaheadLevels - L); // L=1 highest
        const int32_t prio = int32_t(L);

        addresses.emplace_back(pf_addr, prio);
        issued++;

        DPRINTF(HWPrefetch, "%s: prefetch %#lx pc=%#lx L=%u o=%d prio=%d\n",
                name(), pf_addr, pc, L, o, prio);
    }
}

} // namespace prefetch
} // namespace gem5
