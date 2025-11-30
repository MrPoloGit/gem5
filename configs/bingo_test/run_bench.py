import m5
from m5.objects import *
import argparse
import sys

# ------------------------------------------------------------------------------
# 1. Define Cache Classes
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

class L2Cache(Cache):
    size = '256kB'
    assoc = 8
    tag_latency = 20
    data_latency = 20
    response_latency = 20
    mshrs = 20
    tgts_per_mshr = 12

    def __init__(self):
        super().__init__()
        # Prefetcher will be assigned dynamically in the main logic

    def connectCPUSideBus(self, bus):
        self.cpu_side = bus.mem_side_ports

    def connectMemSideBus(self, bus):
        self.mem_side = bus.cpu_side_ports

# ------------------------------------------------------------------------------
# 2. Argument Parsing
# ------------------------------------------------------------------------------

parser = argparse.ArgumentParser(description='Universal Gem5 Prefetcher Benchmark Script')

# --- Simulation Basics ---
parser.add_argument('cmd', nargs='?', default='tests/test-progs/hello/bin/x86/linux/hello',
                    help='Path to the binary executable to run')
parser.add_argument('--options', default='', help='Command line options for the binary')

# --- Prefetcher Selection ---
parser.add_argument('--prefetcher', type=str, default='none',
                    choices=['none', 'bingo', 'mlop', 'bop', 'stride', 'tagged'],
                    help='Type of prefetcher to use on the L2 Cache')

parser.add_argument('--degree', type=int, default=0,
                    help='Degree of prefetching (Overwrites default if > 0). '
                         'Note: For Bingo, this sets "prefetch_degree". '
                         'For others, it sets "degree".')

# --- Specific: Bingo Prefetcher ---
bingo_group = parser.add_argument_group('Bingo Prefetcher Options')
bingo_group.add_argument('--bingo-region-size', type=int, default=4096,
                         help='Spatial region size in bytes (default: 4096)')
bingo_group.add_argument('--bingo-history-len', type=int, default=8,
                         help='Length of event history sequence (default: 8)')
bingo_group.add_argument('--bingo-buckets', type=int, default=16,
                         help='Number of event buckets (default: 16)')
bingo_group.add_argument('--bingo-patterns', type=int, default=512,
                         help='Max pattern table entries (default: 512)')

# --- Specific: MLOP Prefetcher ---
mlop_group = parser.add_argument_group('MLOP Prefetcher Options')
mlop_group.add_argument('--mlop-eval-period', type=int, default=500,
                        help='Number of misses between offset evaluations (default: 500)')
mlop_group.add_argument('--mlop-lookahead', type=int, default=16,
                        help='Number of lookahead levels (default: 16)')
mlop_group.add_argument('--mlop-max-offset', type=int, default=32,
                        help='Maximum offset to track (default: 32)')
mlop_group.add_argument('--mlop-score-threshold', type=int, default=200,
                        help='Score threshold for offset selection (default: 200)')

# --- Specific: BOP Prefetcher ---
bop_group = parser.add_argument_group('BOP Prefetcher Options')
bop_group.add_argument('--bop-rr-size', type=int, default=64,
                       help='Number of entries in RR bank (default: 64)')
bop_group.add_argument('--bop-score-max', type=int, default=31,
                       help='Max score to update best offset (default: 31)')
bop_group.add_argument('--bop-round-max', type=int, default=100,
                       help='Max round to update best offset (default: 100)')
bop_group.add_argument('--bop-bad-score', type=int, default=10,
                       help='Score at which HWP is disabled (default: 10)')

args = parser.parse_args()

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

# Create the L2 Cache
system.l2cache = L2Cache()

# ------------------------------------------------------------------------------
# 4. Prefetcher Configuration Logic
# ------------------------------------------------------------------------------

prefetcher = None

if args.prefetcher == 'bingo':
    prefetcher = BingoPrefetcher()
    # Apply specific Bingo args
    prefetcher.region_size = args.bingo_region_size
    prefetcher.event_history_len = args.bingo_history_len
    prefetcher.bucket_count = args.bingo_buckets
    prefetcher.pattern_table_entries = args.bingo_patterns
    # Apply degree
    if args.degree > 0:
        prefetcher.prefetch_degree = args.degree

elif args.prefetcher == 'mlop':
    prefetcher = MLOPPrefetcher()
    # Apply specific MLOP args
    prefetcher.evaluation_period = args.mlop_eval_period
    prefetcher.lookahead_levels = args.mlop_lookahead
    prefetcher.max_offset = args.mlop_max_offset
    prefetcher.score_threshold = args.mlop_score_threshold
    # MLOP doesn't strictly use a 'degree' param for generation count in the standard way,
    # but relies on lookahead_levels. 

elif args.prefetcher == 'bop':
    prefetcher = BOPPrefetcher()
    # Apply specific BOP args
    prefetcher.rr_size = args.bop_rr_size
    prefetcher.score_max = args.bop_score_max
    prefetcher.round_max = args.bop_round_max
    prefetcher.bad_score = args.bop_bad_score
    # Apply degree
    if args.degree > 0:
        prefetcher.degree = args.degree

elif args.prefetcher == 'stride':
    prefetcher = StridePrefetcher()
    if args.degree > 0:
        prefetcher.degree = args.degree

elif args.prefetcher == 'tagged':
    prefetcher = TaggedPrefetcher()
    if args.degree > 0:
        prefetcher.degree = args.degree

elif args.prefetcher == 'none':
    prefetcher = NULL

# Attach the chosen prefetcher to the L2 Cache
system.l2cache.prefetcher = prefetcher

# ------------------------------------------------------------------------------
# 5. Connect Memory System
# ------------------------------------------------------------------------------

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

# Connect system port
system.system_port = system.membus.cpu_side_ports

# Create Memory Controller
system.mem_ctrl = MemCtrl()
system.mem_ctrl.dram = DDR3_1600_8x8()
system.mem_ctrl.dram.range = system.mem_ranges[0]
system.mem_ctrl.port = system.membus.mem_side_ports

# ------------------------------------------------------------------------------
# 6. Workload and Execution
# ------------------------------------------------------------------------------

# Initialize SE Workload
system.workload = SEWorkload.init_compatible(args.cmd)

# Set up the process
process = Process()
process.cmd = [args.cmd]

if args.options:
    process.cmd.extend(args.options.split())

system.cpu.workload = process
system.cpu.createThreads()

# Instantiate the system
print(f"--- Info: Instantiating system ---")
print(f"    Binary: {args.cmd}")
print(f"    Prefetcher: {args.prefetcher}")
if args.degree > 0:
    print(f"    Degree: {args.degree}")

root = Root(full_system=False, system=system)
m5.instantiate()

print("--- Start Simulation ---")
exit_event = m5.simulate()

print(f'Exiting @ tick {m5.curTick()} because {exit_event.getCause()}')
