import os
import matplotlib.pyplot as plt
import numpy as np

# Configuration
M5OUT_DIR = "m5out"  # The root folder containing the prefetcher folders
OUTPUT_DIR = "stat_graphs" # Where to save the resulting images

# Hardcoded Prefetcher Order (Display Names)
PREFETCHER_ORDER = ["None", "Stride", "Bingo", "BOP", "MLOP"]

# Hardcoded Colors using RGB Tuples (R, G, B)
# Values range from 0.0 to 1.0
PREFETCHER_COLORS = {
    "None":   (0.121, 0.466, 0.705), # Muted Blue
    "Stride": (1.000, 0.498, 0.054), # Safety Orange
    "Bingo":  (0.172, 0.627, 0.172), # Cooked Asparagus Green
    "BOP":    (0.580, 0.403, 0.741),  # Muted Purple
    "MLOP":   (0.839, 0.152, 0.156) # Brick Red
}

# Map specific gem5 stat keys to readable names
METRICS = [
    {
        "name": "IPC",
        "file_key": "system.cpu.ipc",
        "is_ratio": False,
        "prefetch_related": False
    },
    {
        "name": "Prefetch Accuracy",
        "file_key": "system.l2cache.prefetcher.accuracy",
        "is_ratio": False,
        "prefetch_related": True
    },
    {
        "name": "Prefetch Coverage",
        "file_key": "system.l2cache.prefetcher.coverage",
        "is_ratio": False,
        "prefetch_related": True
    },
    {
        "name": "Late Prefetches Ratio",
        "numerator": "system.l2cache.prefetcher.pfLate",
        "denominator": "system.l2cache.prefetcher.pfIssued",
        "is_ratio": True,
        "prefetch_related": True
    },
    {
        "name": "Unused Prefetches Ratio",
        "numerator": "system.l2cache.prefetcher.pfUnused",
        "denominator": "system.l2cache.prefetcher.pfIssued",
        "is_ratio": True,
        "prefetch_related": True
    },
    {
        "name": "Total DRAM Read Accesses",
        "file_key": "system.mem_ctrl.dram.numReads::total",
        "is_ratio": False,
        "prefetch_related": False
    }
]

def parse_stats_file(filepath):
    """
    Parses a gem5 stats.txt file and returns a dictionary of all stats.
    """
    stats = {}
    if not os.path.exists(filepath):
        return stats

    with open(filepath, 'r') as f:
        for line in f:
            if not line.strip() or line.startswith('---'):
                continue
            parts = line.split()
            if len(parts) >= 2:
                try:
                    stats[parts[0]] = float(parts[1])
                except ValueError:
                    continue
    return stats

def collect_data():
    """
    Traverses m5out/Prefetcher/Workload/stats.txt structure.
    Returns structured data: data[workload][prefetcher_folder_name][metric_name] = value
    """
    data = {}
    
    if not os.path.exists(M5OUT_DIR):
        print(f"Error: Directory '{M5OUT_DIR}' not found.")
        return None

    prefetchers = [d for d in os.listdir(M5OUT_DIR) if os.path.isdir(os.path.join(M5OUT_DIR, d))]
    
    if not prefetchers:
        print("No prefetcher directories found.")
        return None

    for prefetcher in prefetchers:
        prefetcher_path = os.path.join(M5OUT_DIR, prefetcher)
        workloads = [d for d in os.listdir(prefetcher_path) if os.path.isdir(os.path.join(prefetcher_path, d))]
        
        for workload in workloads:
            if workload not in data:
                data[workload] = {}
            
            stats_file = os.path.join(prefetcher_path, workload, "stats.txt")
            raw_stats = parse_stats_file(stats_file)
            
            data[workload][prefetcher] = {}
            
            for metric in METRICS:
                val = 0.0
                if metric["is_ratio"]:
                    num = raw_stats.get(metric["numerator"], 0.0)
                    denom = raw_stats.get(metric["denominator"], 0.0)
                    val = num / denom if denom > 0 else 0.0
                else:
                    val = raw_stats.get(metric["file_key"], 0.0)
                
                data[workload][prefetcher][metric["name"]] = val

    return data

def calculate_geomean(values):
    a = np.array(values)
    if len(a) == 0:
        return 0.0
    if np.any(a <= 0):
        return 0.0
    return np.exp(np.mean(np.log(a)))

def add_average_group(data):
    if not data:
        return

    all_prefetchers = set()
    for w in data:
        all_prefetchers.update(data[w].keys())
    
    average_data = {p: {} for p in all_prefetchers}
    temp_values = {p: {m['name']: [] for m in METRICS} for p in all_prefetchers}

    for workload in data:
        for prefetcher in all_prefetchers:
            p_data = data[workload].get(prefetcher, {})
            for metric in METRICS:
                m_name = metric['name']
                val = p_data.get(m_name, 0.0)
                temp_values[prefetcher][m_name].append(val)

    for prefetcher in temp_values:
        for m_name, values in temp_values[prefetcher].items():
            average_data[prefetcher][m_name] = calculate_geomean(values)

    data['Average'] = average_data

def get_prefetcher_mapping(available_prefetchers):
    """
    Creates a mapping from {Display Name -> Real Folder Name}
    based on case-insensitive matching with PREFETCHER_ORDER.
    """
    mapping = {}
    available_map = {p.lower(): p for p in available_prefetchers}
    
    for display_name in PREFETCHER_ORDER:
        target_lower = display_name.lower()
        if target_lower in available_map:
            mapping[display_name] = available_map[target_lower]
            
    return mapping

def plot_percent_improvement(data, target_metric="IPC", baseline_name="None"):
    if not data:
        return

    if not os.path.exists(OUTPUT_DIR):
        os.makedirs(OUTPUT_DIR)

    workload_keys = [k for k in data.keys() if k != 'Average']
    workloads = sorted(workload_keys)
    if 'Average' in data:
        workloads.append('Average')

    all_prefetchers_in_data = set()
    for w in data:
        all_prefetchers_in_data.update(data[w].keys())
    
    pf_map = get_prefetcher_mapping(all_prefetchers_in_data)
    actual_baseline_key = pf_map.get(baseline_name)
    
    if not actual_baseline_key:
        print(f"Skipping improvement plot: Baseline '{baseline_name}' not found.")
        return

    sorted_others = []
    for display_name in PREFETCHER_ORDER:
        if display_name == baseline_name:
            continue
        if display_name in pf_map:
            sorted_others.append(display_name)

    if not sorted_others:
        print("Skipping improvement plot: No other prefetchers to compare.")
        return

    x = np.arange(len(workloads))
    width = 0.8 / len(sorted_others)
    fig, ax = plt.subplots(figsize=(14, 6))

    ax.grid(axis='y', linestyle='--', alpha=0.7)
    ax.axhline(0, color='black', linewidth=0.8)

    for i, display_name in enumerate(sorted_others):
        actual_key = pf_map[display_name]
        
        # Determine Color
        bar_color = PREFETCHER_COLORS.get(display_name, (0.5, 0.5, 0.5)) # Default to gray

        y_values = []
        for workload in workloads:
            base_val = data.get(workload, {}).get(actual_baseline_key, {}).get(target_metric, 0.0)
            curr_val = data.get(workload, {}).get(actual_key, {}).get(target_metric, 0.0)

            if base_val > 0:
                pct_improv = ((curr_val - base_val) / base_val) * 100.0
            else:
                pct_improv = 0.0
            
            y_values.append(pct_improv)

        offset = width * i
        centering = (width * len(sorted_others)) / 2
        ax.bar(x + offset - centering + (width/2), y_values, width, label=display_name, color=bar_color, zorder=3)

    ax.set_xlabel('Workloads')
    ax.set_ylabel(f'% Improvement over {baseline_name}')
    ax.set_title(f'{target_metric} Percent Improvement over {baseline_name}')
    ax.set_xticks(x)
    ax.set_xticklabels(workloads)
    ax.legend(title="Prefetchers", bbox_to_anchor=(1.01, 1), loc='upper left')
    
    plt.tight_layout()
    filename = f"{target_metric.lower()}_percent_improvement.png"
    save_path = os.path.join(OUTPUT_DIR, filename)
    plt.savefig(save_path, dpi=300)
    print(f"Generated graph: {save_path}")
    plt.close()

def plot_data(data):
    if not data:
        return

    if not os.path.exists(OUTPUT_DIR):
        os.makedirs(OUTPUT_DIR)

    workload_keys = [k for k in data.keys() if k != 'Average']
    workloads = sorted(workload_keys)
    if 'Average' in data:
        workloads.append('Average')
    
    all_prefetchers_in_data = set()
    for w in data:
        all_prefetchers_in_data.update(data[w].keys())

    pf_map = get_prefetcher_mapping(all_prefetchers_in_data)

    for metric in METRICS:
        metric_name = metric["name"]
        
        prefetchers_to_plot = [] 
        
        for display_name in PREFETCHER_ORDER:
            if display_name not in pf_map:
                continue
                
            if metric["prefetch_related"] and display_name.lower() == "none":
                continue
            
            prefetchers_to_plot.append((display_name, pf_map[display_name]))
            
        if not prefetchers_to_plot:
            print(f"Skipping plot for {metric_name}: No relevant prefetchers found.")
            continue

        x = np.arange(len(workloads))
        width = 0.8 / len(prefetchers_to_plot)
        fig, ax = plt.subplots(figsize=(14, 6))

        ax.grid(axis='y', linestyle='--', alpha=0.7)
        
        for i, (display_name, actual_key) in enumerate(prefetchers_to_plot):
            # Determine Color
            bar_color = PREFETCHER_COLORS.get(display_name, (0.5, 0.5, 0.5))

            y_values = []
            for workload in workloads:
                val = data.get(workload, {}).get(actual_key, {}).get(metric_name, 0.0)
                y_values.append(val)
            
            offset = width * i
            centering = (width * len(prefetchers_to_plot)) / 2
            
            ax.bar(x + offset - centering + (width/2), y_values, width, label=display_name, color=bar_color, zorder=3)

        ax.set_xlabel('Workloads')
        ax.set_ylabel(metric_name)
        ax.set_title(f'{metric_name} by Workload')
        ax.set_xticks(x)
        ax.set_xticklabels(workloads)
        ax.legend(title="Prefetchers", bbox_to_anchor=(1.01, 1), loc='upper left')
        
        plt.tight_layout()
        filename = f"{metric_name.replace(' ', '_').lower()}.png"
        save_path = os.path.join(OUTPUT_DIR, filename)
        plt.savefig(save_path, dpi=300)
        print(f"Generated graph: {save_path}")
        plt.close()

if __name__ == "__main__":
    print(f"Scanning directory: {M5OUT_DIR}...")
    processed_data = collect_data()
    
    if processed_data:
        print(f"Found {len(processed_data)} workloads. Calculating geometric means...")
        add_average_group(processed_data)
        
        plot_data(processed_data)
        
        print("Generating improvement graph...")
        plot_percent_improvement(processed_data, target_metric="IPC", baseline_name="None")
        
        print("Done!")
