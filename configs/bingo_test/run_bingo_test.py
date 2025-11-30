import m5
from m5.objects import *
import argparse
import sys

# ------------------------------------------------------------------------------
# 1. Define Custom Cache Classes
# ------------------------------------------------------------------------------

class L1ICache(Cache):
    size = '16kB'
    assoc = 2
    tag_latency = 2
    data_latency = 2
    response_latency = 2
    mshrs = 4
    tgts_per_mshr = 20
    
    def __init__(self):
        super().__init__()

    def connectCPU(self, cpu):
        self.cpu_side = cpu.icache_port

    def connectBus(self, bus):
        self.mem_side = bus.cpu_side_ports

class L1DCache(Cache):
    size = '64kB'
    assoc = 2
    tag_latency = 2
    data_latency = 2
    response_latency = 2
    mshrs = 4
    tgts_per_mshr = 20
    
    def __init__(self):
        super().__init__()

    def connectCPU(self, cpu):
        self.cpu_side = cpu.dcache_port

    def connectBus(self, bus):
        self.mem_side = bus.cpu_side_ports

class L2CacheWithBingo(Cache):
    size = '256kB'
    assoc = 8
    tag_latency = 20
    data_latency = 20
    response_latency = 20
    mshrs = 20
    tgts_per_mshr = 12

    def __init__(self, options=None):
        super().__init__()
        
        # Instantiate the Bingo Prefetcher
        # The parameters here match those defined in your Prefetcher.py
        self.prefetcher = BingoPrefetcher()

        # Apply command line options if provided
        if options:
            self.prefetcher.region_size = options.region_size
            self.prefetcher.event_history_len = options.event_history_len
            self.prefetcher.bucket_count = options.bucket_count
            self.prefetcher.pattern_table_entries = options.pattern_table_entries
            self.prefetcher.prefetch_degree = options.prefetch_degree

    def connectCPUSideBus(self, bus):
        self.cpu_side = bus.mem_side_ports

    def connectMemSideBus(self, bus):
        self.mem_side = bus.cpu_side_ports

# ------------------------------------------------------------------------------
# 2. Parse Arguments
# ------------------------------------------------------------------------------

parser = argparse.ArgumentParser(description='Test Bingo Prefetcher on L2 Cache')

# Binary to run (default to hello world)
parser.add_argument('cmd', nargs='?', default='tests/test-progs/hello/bin/x86/linux/hello',
                    help='Path to the binary executable to run')

# Bingo Specific Parameters
parser.add_argument('--region_size', type=int, default=4096,
                    help='Spatial region size in bytes (default: 4096)')
parser.add_argument('--event_history_len', type=int, default=8,
                    help='Length of event history sequence (default: 8)')
parser.add_argument('--bucket_count', type=int, default=16,
                    help='Number of event buckets (default: 16)')
parser.add_argument('--pattern_table_entries', type=int, default=512,
                    help='Max pattern table entries (default: 512)')
parser.add_argument('--prefetch_degree', type=int, default=2,
                    help='Degree of prefetching (default: 2)')
parser.add_argument('--prefetcher', type=str, default='bingo', choices=['bingo', 'stride', 'none'],
                    help='Type of prefetcher to use')

options = parser.parse_args()

# ------------------------------------------------------------------------------
# 3. System Construction
# ------------------------------------------------------------------------------

# Create the system
system = System()

# Set the clock (1GHz)
system.clk_domain = SrcClockDomain()
system.clk_domain.clock = '1GHz'
system.clk_domain.voltage_domain = VoltageDomain()

# Set up memory mode and ranges
system.mem_mode = 'timing'
system.mem_ranges = [AddrRange('512MB')]

# Create a simple CPU
system.cpu = X86TimingSimpleCPU()

# Create the L1 Caches
system.cpu.icache = L1ICache()
system.cpu.dcache = L1DCache()

# Create the L2 Bus
system.l2bus = L2XBar()

# Connect L1 to CPU and L2 Bus
system.cpu.icache.connectCPU(system.cpu)
system.cpu.icache.connectBus(system.l2bus)
system.cpu.dcache.connectCPU(system.cpu)
system.cpu.dcache.connectBus(system.l2bus)

# Create the L2 Cache with BINGO attached
system.l2cache = L2CacheWithBingo(options)

if options.prefetcher == 'bingo':
    system.l2cache.prefetcher = BingoPrefetcher()
    # ... apply bingo specific options ...
elif options.prefetcher == 'stride':
    system.l2cache.prefetcher = StridePrefetcher()
elif options.prefetcher == 'none':
    system.l2cache.prefetcher = NULL

# Connect L2 to L2 Bus
system.l2cache.connectCPUSideBus(system.l2bus)

# Create the Memory Bus
system.membus = SystemXBar()

# Connect L2 to Memory Bus
system.l2cache.connectMemSideBus(system.membus)

# Connect the Interrupt Controller
system.cpu.createInterruptController()
system.cpu.interrupts[0].pio = system.membus.mem_side_ports
system.cpu.interrupts[0].int_requestor = system.membus.cpu_side_ports
system.cpu.interrupts[0].int_responder = system.membus.mem_side_ports

# connect system port
system.system_port = system.membus.cpu_side_ports

# Create Memory Controller
system.mem_ctrl = MemCtrl()
system.mem_ctrl.dram = DDR3_1600_8x8()
system.mem_ctrl.dram.range = system.mem_ranges[0]
system.mem_ctrl.port = system.membus.mem_side_ports

# ------------------------------------------------------------------------------
# 4. Workload and Execution
# ------------------------------------------------------------------------------

# FIX: Explicitly initialize the SE workload based on the binary
# This tells gem5 to load the x86 syscall emulation layer
system.workload = SEWorkload.init_compatible(options.cmd)

# Set up the process
process = Process()
process.cmd = [options.cmd]
system.cpu.workload = process
system.cpu.createThreads()

# Instantiate the system
print(f"Instantiating system with Bingo Prefetcher (Degree: {options.prefetch_degree})...")
root = Root(full_system=False, system=system)
m5.instantiate()

print("Beginning simulation!")
exit_event = m5.simulate()

print(f'Exiting @ tick {m5.curTick()} because {exit_event.getCause()}')
