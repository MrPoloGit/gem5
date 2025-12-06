/**
* https://mshakerinava.github.io/papers/mlop-dpc3.pdf
* Multi-Lookahead Offset Prefetching (MLOP) - paper-matching version.
*
* Key paper defaults:
*  - AMT entries: 256
*  - Neighborhood bit-vector: 64 lines
*  - Lookahead levels: 16  (recent list stores last 15 indices)
*  - Evaluation period: 500 L1-D misses
*
* Trains/acts on miss stream only (PrefetchInfo::isCacheMiss()).
*/


#ifndef __MEM_CACHE_PREFETCH_MLOP_HH__
#define __MEM_CACHE_PREFETCH_MLOP_HH__


#include <array>
#include <cstdint>
#include <unordered_map>
#include <utility>
#include <vector>

#include "base/types.hh"
#include "mem/cache/prefetch/queued.hh"

namespace gem5
{


struct MLOPPrefetcherParams;


namespace prefetch
{


class MLOP : public Queued
{
 public:
   MLOP(const MLOPPrefetcherParams &p);
   ~MLOP() = default;


   void calculatePrefetch(const PrefetchInfo &pfi,
                          std::vector<AddrPriority> &addresses,
                          const CacheAccessor &cache) override;


 private:
   // Paper constants (fixed to match the DPC3 MLOP description)
   static constexpr unsigned RegionBits = 6;                 // 2^6 = 64 lines
   static constexpr unsigned RegionSize = 1u << RegionBits;  // 64
   static constexpr unsigned RegionMask = RegionSize - 1u;   // 63


   static constexpr unsigned PaperAmtEntries = 256;
   static constexpr unsigned PaperLookaheadLevels = 16;
   static constexpr unsigned PaperEvalPeriod = 500;


   // For lookahead N, paper keeps last (N-1) accesses to exclude them for L>1
   static constexpr unsigned RecentDepth = PaperLookaheadLevels - 1; // 15


   // Candidate offsets must fit inside [-(RegionSize-1), +(RegionSize-1)]
   static constexpr int PaperMaxOffset = int(RegionSize) - 1; // 63


   struct OffsetEntry
   {
       int offset;                   // signed offset in blocks (cache lines)
       std::array<uint32_t, PaperLookaheadLevels> scores; // scores[L-1]
   };


   struct AMTEntry
   {
       bool valid = false;
       Addr base_block = 0;          // block address with low 6 bits cleared
       uint64_t bv = 0;              // 64-bit neighborhood bit-vector


       // Most-recent-first indices [0..63], length <= RecentDepth
       std::array<uint8_t, RecentDepth> recent{};
       uint8_t recent_len = 0;


       uint64_t last_touch = 0;      // LRU replacement
   };


   // Parameters (paper-fixed; we still accept params object for gem5
   // plumbing)
   const unsigned evalPeriod;
   const unsigned lookaheadLevels;
   const int maxOffset;
   const unsigned scoreThreshold;


   // State
   std::vector<AMTEntry> amt;  // AMT with LRU replacement, size=256
   uint64_t amtClock = 0;


   // offset -> scores
   std::unordered_map<int, OffsetEntry> offsetTable;
   std::vector<std::pair<unsigned, const OffsetEntry*>> bestOffsets;


   unsigned missCounter = 0;


   // Helpers
   void resetScores();
   void selectBestOffsets();


   AMTEntry &findOrAllocAmtEntry(Addr base_block);
   void updateScoresWithMiss(Addr block);
};


} // namespace prefetch
} // namespace gem5


#endif // __MEM_CACHE_PREFETCH_MLOP_HH__
