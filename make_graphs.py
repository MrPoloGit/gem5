import os
import matplotlib.pyplot as plt
import numpy as np

# Configuration
M5OUT_DIR = "m5out"  # The root folder containing the prefetcher folders
OUTPUT_DIR = "stat_graphs" # Where to save the resulting images

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
        print(f"Warning: File not found: {filepath}")
        return stats

    with open(filepath, 'r') as f:
        for line in f:
            # Skip empty lines or headers
            if not line.strip() or line.startswith('---'):
                continue
            
            parts = line.split()
            if len(parts) >= 2:
                try:
                    # parts[0] is the stat name, parts[1] is the value
                    stats[parts[0]] = float(parts[1])
                except ValueError:
                    continue
    return stats

def collect_data():
    """
    Traverses m5out/Prefetcher/Workload/stats.txt structure.
    Returns structured data: data[workload][prefetcher][metric_name] = value
    """
    data = {}
    
    # Check if root directory exists
    if not os.path.exists(M5OUT_DIR):
        print(f"Error: Directory '{M5OUT_DIR}' not found.")
        return None

    # Get list of prefetcher folders
    prefetchers = [d for d in os.listdir(M5OUT_DIR) if os.path.isdir(os.path.join(M5OUT_DIR, d))]
    
    if not prefetchers:
        print("No prefetcher directories found.")
        return None

    for prefetcher in prefetchers:
        prefetcher_path = os.path.join(M5OUT_DIR, prefetcher)
        
        # Get list of workload subfolders (the 2 letter names)
        workloads = [d for d in os.listdir(prefetcher_path) if os.path.isdir(os.path.join(prefetcher_path, d))]
        
        for workload in workloads:
            if workload not in data:
                data[workload] = {}
            
            stats_file = os.path.join(prefetcher_path, workload, "stats.txt")
            raw_stats = parse_stats_file(stats_file)
            
            data[workload][prefetcher] = {}
            
            # Calculate values for each requested metric
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
    """
    Calculates geometric mean of a list of values.
    Returns 0.0 if any value is <= 0 (standard definition strictness for this context).
    """
    a = np.array(values)
    if len(a) == 0:
        return 0.0
    
    # Handle zeros: if any value is 0, geometric mean is 0
    if np.any(a <= 0):
        return 0.0
        
    # Geometric mean = exp(mean(log(x)))
    return np.exp(np.mean(np.log(a)))

def add_average_group(data):
    """
    Calculates the GEOMETRIC MEAN for each metric across all workloads 
    and adds an 'Average' entry to the data dictionary.
    """
    if not data:
        return

    # Identify all prefetchers present in the data
    all_prefetchers = set()
    for w in data:
        all_prefetchers.update(data[w].keys())
    
    # Create structure for average data
    average_data = {p: {} for p in all_prefetchers}

    # Collect lists of values to calculate gmean later
    temp_values = {p: {m['name']: [] for m in METRICS} for p in all_prefetchers}

    for workload in data:
        for prefetcher in all_prefetchers:
            p_data = data[workload].get(prefetcher, {})
            for metric in METRICS:
                m_name = metric['name']
                val = p_data.get(m_name, 0.0)
                temp_values[prefetcher][m_name].append(val)

    # Calculate Geometric Mean
    for prefetcher in temp_values:
        for m_name, values in temp_values[prefetcher].items():
            average_data[prefetcher][m_name] = calculate_geomean(values)

    # Add to main data structure
    data['Average'] = average_data

def find_baseline_key(prefetcher_keys, baseline_name="none"):
    """Helper to find the actual dictionary key for 'none' (case insensitive)."""
    for key in prefetcher_keys:
        if key.lower() == baseline_name.lower():
            return key
    return None

def plot_percent_improvement(data, target_metric="IPC", baseline_name="none"):
    """
    Generates a graph showing % improvement over the baseline for a specific metric.
    """
    if not data:
        return

    # Check output directory
    if not os.path.exists(OUTPUT_DIR):
        os.makedirs(OUTPUT_DIR)

    # Sort workloads, putting Average at end
    workload_keys = [k for k in data.keys() if k != 'Average']
    workloads = sorted(workload_keys)
    if 'Average' in data:
        workloads.append('Average')

    # Identify all prefetchers
    all_prefetchers_in_data = set()
    for w in data:
        all_prefetchers_in_data.update(data[w].keys())
    
    # Find the actual key used for the baseline (e.g., "None", "none", "NONE")
    actual_baseline_key = find_baseline_key(all_prefetchers_in_data, baseline_name)
    
    if not actual_baseline_key:
        print(f"Skipping improvement plot: Baseline '{baseline_name}' not found in data.")
        return

    # Filter prefetchers: exclude the baseline itself from the bars
    sorted_others = sorted([p for p in all_prefetchers_in_data if p != actual_baseline_key])

    if not sorted_others:
        print("Skipping improvement plot: No other prefetchers to compare against baseline.")
        return

    # Setup plot
    x = np.arange(len(workloads))
    width = 0.8 / len(sorted_others)
    fig, ax = plt.subplots(figsize=(14, 6))

    # Generate bars
    for i, prefetcher in enumerate(sorted_others):
        y_values = []
        for workload in workloads:
            # Get Baseline Value
            base_val = data.get(workload, {}).get(actual_baseline_key, {}).get(target_metric, 0.0)
            # Get Current Prefetcher Value
            curr_val = data.get(workload, {}).get(prefetcher, {}).get(target_metric, 0.0)

            # Calculate Percent Improvement: ((Current - Base) / Base) * 100
            if base_val > 0:
                pct_improv = ((curr_val - base_val) / base_val) * 100.0
            else:
                pct_improv = 0.0 # Avoid divide by zero
            
            y_values.append(pct_improv)

        # Bar positioning
        offset = width * i
        centering = (width * len(sorted_others)) / 2
        ax.bar(x + offset - centering + (width/2), y_values, width, label=prefetcher)

    # Add a horizontal line at 0
    ax.axhline(0, color='black', linewidth=0.8)

    # Formatting
    ax.set_xlabel('Workloads')
    ax.set_ylabel(f'% Improvement over {actual_baseline_key}')
    ax.set_title(f'{target_metric} Percent Improvement over {actual_baseline_key} (Higher is Better)')
    ax.set_xticks(x)
    ax.set_xticklabels(workloads)
    
    # Legend
    ax.legend(title="Prefetchers", bbox_to_anchor=(1.01, 1), loc='upper left')
    
    plt.tight_layout()
    
    # Save
    filename = f"{target_metric.lower()}_percent_improvement.png"
    save_path = os.path.join(OUTPUT_DIR, filename)
    plt.savefig(save_path, dpi=300)
    print(f"Generated graph: {save_path}")
    plt.close()

def plot_data(data):
    if not data:
        return

    # Create output directory
    if not os.path.exists(OUTPUT_DIR):
        os.makedirs(OUTPUT_DIR)

    # Sort workloads and append Average at the end
    workload_keys = [k for k in data.keys() if k != 'Average']
    workloads = sorted(workload_keys)
    if 'Average' in data:
        workloads.append('Average')
    
    # Identify all available prefetchers
    all_prefetchers_in_data = set()
    for w in data:
        all_prefetchers_in_data.update(data[w].keys())
    sorted_all_prefetchers = sorted(list(all_prefetchers_in_data))

    # Generate one graph per metric
    for metric in METRICS:
        metric_name = metric["name"]
        
        # Determine which prefetchers to show for this specific graph
        prefetchers_to_plot = []
        for p in sorted_all_prefetchers:
            # If graph is prefetch_related, skip "none" (case insensitive)
            if metric["prefetch_related"] and p.lower() == "none":
                continue
            prefetchers_to_plot.append(p)
            
        if not prefetchers_to_plot:
            print(f"Skipping plot for {metric_name}: No relevant prefetchers found.")
            continue

        # Setup plot
        x = np.arange(len(workloads))
        width = 0.8 / len(prefetchers_to_plot)
        fig, ax = plt.subplots(figsize=(14, 6))
        
        # Create bars
        for i, prefetcher in enumerate(prefetchers_to_plot):
            y_values = []
            for workload in workloads:
                val = data.get(workload, {}).get(prefetcher, {}).get(metric_name, 0.0)
                y_values.append(val)
            
            # Grouping offset
            offset = width * i
            centering = (width * len(prefetchers_to_plot)) / 2
            
            ax.bar(x + offset - centering + (width/2), y_values, width, label=prefetcher)

        # Formatting
        ax.set_xlabel('Workloads')
        ax.set_ylabel(metric_name)
        ax.set_title(f'{metric_name} by Workload (with Geometric Mean)')
        ax.set_xticks(x)
        ax.set_xticklabels(workloads)
        
        # Legend
        ax.legend(title="Prefetchers", bbox_to_anchor=(1.01, 1), loc='upper left')
        
        plt.tight_layout()
        
        # Save
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
        
        # Plot standard absolute values
        plot_data(processed_data)
        
        # Plot Percent Improvement for IPC
        print("Generating improvement graph...")
        plot_percent_improvement(processed_data, target_metric="IPC", baseline_name="none")
        
        print("Done!")
