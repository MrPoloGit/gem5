/**
 * https://ieeexplore.ieee.org/stamp/stamp.jsp?tp=&arnumber=8675188
 * Describes the Bingo Spatial Data prefetcher based on HPCA 2019 paper.
 */

#include "mem/cache/prefetch/bingo.hh"

#include "debug/HWPrefetch.hh"
#include "params/BingoPrefetcher.hh"

namespace gem5
{

namespace prefetch
{

Bingo::Bingo(const BingoPrefetcherParams &p)
    : Queued(p),
      regionSize(p.region_size),
      regionSizeLog2(floorLog2(p.region_size)),
      accumulationTableEntries(p.accumulation_table_entries),
      historyTableSize(p.history_table_entries),
      historyTableAssoc(p.history_table_assoc)
{
    // Calculate blocks per region based on system block size
    blocksPerRegion = regionSize / blkSize;

    // Initialize History Table (Sets x Ways)
    // Number of sets = Total Entries / Associativity
    int numSets = historyTableSize / historyTableAssoc;
    historyTable.resize(numSets);
    for (auto &set : historyTable) {
        set.resize(historyTableAssoc);
        for (auto &entry : set) {
            entry.footprint.resize(blocksPerRegion, false);
        }
    }
}

Addr
Bingo::pageAddress(Addr addr) const
{
    return addr & ~(Addr)(regionSize - 1);
}

Addr
Bingo::pageOffset(Addr addr) const
{
    return (addr & (regionSize - 1)) / blkSize;
}

uint32_t
Bingo::hashShortEvent(Addr pc, Addr offset) const
{
    // Simple hash combining PC and Offset to index the table
    // Shifting PC to align with typical instruction spacing
    return ((pc >> 1) ^ offset) % historyTable.size();
}

void
Bingo::notifyEvict(const EvictionInfo &info)
{
    // Maintenance typically handled in calculatePrefetch for this design.
}

void
Bingo::calculatePrefetch(const PrefetchInfo &pfi,
                         std::vector<AddrPriority> &addresses,
                         const CacheAccessor &cache)
{
    if (!pfi.hasPC()) {
        return;
    }

    Addr pc = pfi.getPC();
    Addr addr = blockAddress(pfi.getAddr());
    Addr pageAddr = pageAddress(addr);
    Addr offset = pageOffset(addr);

    // -------------------------------------------------------------------------
    // 1. Accumulation / Training Phase
    // -------------------------------------------------------------------------

    auto it = accumulationTable.find(pageAddr);

    if (it != accumulationTable.end()) {
        // Page already active, update footprint
        it->second.footprint[offset] = true;
        it->second.lastAccess = curTick();
    } else {
        // New Page Access (Trigger Access)

        // Check capacity of Accumulation Table
        if (accumulationTable.size() >= accumulationTableEntries) {
            // Evict LRU from Accumulation Table to History Table
            Addr lruPage = 0;
            Tick minTick = MaxTick;

            for (auto &entry : accumulationTable) {
                if (entry.second.lastAccess < minTick) {
                    minTick = entry.second.lastAccess;
                    lruPage = entry.first;
                }
            }

            // Move LRU entry to History Table
            ActiveRegionEntry &lruEntry = accumulationTable.at(lruPage);

            // Index with Short Event (Trigger PC + Trigger Offset)
            uint32_t setIdx = hashShortEvent(lruEntry.pc, lruEntry.offset);

            // Find Victim in History Table (LRU)
            int victimWay = -1;
            Tick minHistTick = MaxTick;
            int invalidWay = -1;

            for (int w = 0; w < historyTableAssoc; ++w) {
                if (!historyTable[setIdx][w].valid) {
                    invalidWay = w;
                    break;
                }
                if (historyTable[setIdx][w].lastUse < minHistTick) {
                    minHistTick = historyTable[setIdx][w].lastUse;
                    victimWay = w;
                }
            }

            int way = (invalidWay != -1) ? invalidWay : victimWay;

            // Store in History Table
            // TAG with Long Event (Trigger PC + Trigger Address/Page)
            historyTable[setIdx][way].valid = true;
            historyTable[setIdx][way].pc = lruEntry.pc;
            historyTable[setIdx][way].address = lruPage; // Storing Page Base
            historyTable[setIdx][way].offset = lruEntry.offset;
            historyTable[setIdx][way].footprint = lruEntry.footprint;
            historyTable[setIdx][way].lastUse = curTick();

            accumulationTable.erase(lruPage);
        }

        // Create new entry in Accumulation Table
        ActiveRegionEntry newEntry(blocksPerRegion);
        newEntry.pc = pc;           // Record Trigger PC
        newEntry.offset = offset;   // Record Trigger Offset
        newEntry.footprint[offset] = true;
        newEntry.lastAccess = curTick();
        accumulationTable.insert({pageAddr, newEntry});
    }

    // -------------------------------------------------------------------------
    // 2. Prediction Phase
    // -------------------------------------------------------------------------

    uint32_t setIdx = hashShortEvent(pc, offset);

    std::vector<PatternEntry> &set = historyTable[setIdx];

    PatternEntry* bestMatch = nullptr;
    std::vector<PatternEntry*> shortEventMatches;

    // Search the set
    for (auto &entry : set) {
        if (!entry.valid) continue;

        // Check for Long Event Match (PC + Address)
        bool pcMatch = (entry.pc == pc);
        bool offsetMatch = (entry.offset == offset);
        bool addrMatch = (entry.address == pageAddr);

        if (pcMatch && addrMatch && offsetMatch) {
            // Exact Long Event Match (Highest Accuracy)
            bestMatch = &entry;
            break; // Found best, stop.
        }

        if (pcMatch && offsetMatch) {
            // Short Event Match
            shortEventMatches.push_back(&entry);
        }
    }

    std::vector<bool> finalFootprint(blocksPerRegion, false);
    bool foundPrediction = false;

    if (bestMatch) {
        // If match found with Long Event, use it.
        finalFootprint = bestMatch->footprint;
        bestMatch->lastUse = curTick();
        foundPrediction = true;
    } else if (!shortEventMatches.empty()) {
        // If no Long match, use voting on Short matches.
        int threshold = (shortEventMatches.size() * 20) / 100;
        if (threshold == 0) threshold = 1;

        for (unsigned blk = 0; blk < blocksPerRegion; ++blk) {
            int votes = 0;
            for (auto *entry : shortEventMatches) {
                if (entry->footprint[blk]) {
                    votes++;
                }
            }

            if (votes >= threshold) {
                finalFootprint[blk] = true;
            }
        }

        // Update LRU for all participating entries
        for (auto *entry : shortEventMatches) {
            entry->lastUse = curTick();
        }
        foundPrediction = true;
    }

    // -------------------------------------------------------------------------
    // 3. Issue Prefetches
    // -------------------------------------------------------------------------

    if (foundPrediction) {
        for (unsigned blk = 0; blk < blocksPerRegion; ++blk) {
            if (finalFootprint[blk]) {
                // Determine prefetch address relative to current page
                Addr prefAddr = pageAddr + (blk * blkSize);

                // Don't prefetch the current block (redundant)
                if (prefAddr == addr) continue;

                // Priority 0 is standard
                addresses.push_back(AddrPriority(prefAddr, 0));
            }
        }
    }
}

} // namespace prefetch
} // namespace gem5
