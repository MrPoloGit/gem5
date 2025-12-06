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
     evalPeriod(PaperEvalPeriod),
     lookaheadLevels(PaperLookaheadLevels),
     maxOffset(PaperMaxOffset),
     scoreThreshold(p.score_threshold),
     amt(PaperAmtEntries)
{
   static_assert(
       PaperLookaheadLevels == 16,
       "PaperLookaheadLevels must be 16"
   );
   static_assert(
       RegionSize == 64,
       "RegionSize must be 64"
   );
   static_assert(
       RecentDepth == 15,
       "RecentDepth must be 15"
   );


   if (scoreThreshold == 0) {
       // With scoreThreshold==0, everything qualifies;
       // that's usually not desired.
       fatal("%s: score_threshold must be > 0\n", name());
   }


   // Populate offset table: [-63, +63], excluding 0,
   // with 16 lookahead scores.
   for (int o = -maxOffset; o <= maxOffset; ++o) {
       if (o == 0) continue;


       OffsetEntry e;
       e.offset = o;
       e.scores.fill(0);
       offsetTable.emplace(o, e);
   }


   DPRINTF(HWPrefetch,
           "%s: init evalPeriod=%u lookaheadLevels=%u maxOffset=%d "
           "scoreThreshold=%u offsets=%zu blkSize=%u AMT=%u regionLines=%u\n",
           name(), evalPeriod, lookaheadLevels, maxOffset, scoreThreshold,
           offsetTable.size(), blkSize, (unsigned)amt.size(), RegionSize);
}

void
MLOP::resetScores()
{
   for (auto &kv : offsetTable) {
       kv.second.scores.fill(0);
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
   v.recent_len = 0;
   v.last_touch = ++amtClock;
   return v;
}

static inline unsigned
ctz64(uint64_t x)
{
   return (unsigned)__builtin_ctzll(x);
}

void
MLOP::updateScoresWithMiss(Addr block)
{
   // block = addr >> lBlkSize (cache-line index)
   const Addr base_block = block & ~Addr(RegionMask);
   const unsigned idx = unsigned(block & Addr(RegionMask)); // 0..63


   AMTEntry &entry = findOrAllocAmtEntry(base_block);

   for (unsigned L = 1; L <= lookaheadLevels; ++L) {
       uint64_t masked = entry.bv;


       const unsigned exclude = (L > 1) ? (L - 1) : 0;
       const unsigned ex = std::min<unsigned>(exclude, entry.recent_len);
       for (unsigned r = 0; r < ex; ++r) {
           masked &= ~(1ULL << entry.recent[r]);
       }


       uint64_t bits = masked;
       while (bits) {
           const unsigned j = ctz64(bits);
           bits &= (bits - 1); // clear lowest set bit

           // signed offset in blocks (cache lines)
           const int k = int(idx) - int(j);
           if (k == 0) continue;
           if (k < -maxOffset || k > maxOffset) continue;

           auto it = offsetTable.find(k);
           if (it != offsetTable.end()) {
               it->second.scores[L - 1]++;
           }
       }
   }

   // Update ordered recent list (most-recent-first), max length = 15.
   if (entry.recent_len < RecentDepth) {
       entry.recent_len++;
   }
   for (int i = int(entry.recent_len) - 1; i > 0; --i) {
       entry.recent[i] = entry.recent[i - 1];
   }
   if (RecentDepth > 0) {
       entry.recent[0] = uint8_t(idx);
   }

   // Finally set the bit for this access (do after
   // scoring to avoid self-credit).
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

       // store best offset for this lookahead level
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
   // Paper: trained by L1-D miss streams -> trigger only on cache misses.
   if (!pfi.isCacheMiss())
       return;

   const Addr addr = pfi.getAddr();
   const Addr pc = pfi.hasPC() ? pfi.getPC() : 0;

   const Addr block = addr >> lBlkSize;

   // Train scores on this miss.
   updateScoresWithMiss(block);
   missCounter++;

   // At the end of each evaluation period (500 misses),
   // choose best offsets and reset epoch scores.
   if (missCounter >= evalPeriod) {
       selectBestOffsets();
       resetScores();
       missCounter = 0;

       DPRINTF(HWPrefetch, "%s: epoch done, bestOffsets=%zu\n",
               name(), bestOffsets.size());
   }

   if (bestOffsets.empty())
       return;

   // Prefetch in increasing lookahead order (timeliness prioritization).
   for (const auto &p : bestOffsets) {
       const unsigned L = p.first;
       const OffsetEntry *e = p.second;

       // already the chosen distance for that lookahead
       const int o = e->offset;
       const Addr pf_addr = addr + (Addr(o) << lBlkSize);

       addresses.emplace_back(pf_addr, 0);

       DPRINTF(HWPrefetch, "%s: prefetch %#lx pc=%#lx L=%u o=%d\n",
               name(), pf_addr, pc, L, o);
   }
}

} // namespace prefetch
} // namespace gem5
