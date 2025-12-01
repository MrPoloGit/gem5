/*
 * Copyright (c) 2024 Samsung Electronics
 * Copyright (c) 2019 Sharif University of Technology
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met: redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer;
 * redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution;
 * neither the name of the copyright holders nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * @file
 * Describes the Bingo prefetcher based on HPCA 2019 paper.
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
