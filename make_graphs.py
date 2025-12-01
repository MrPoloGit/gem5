import os
import matplotlib.pyplot as plt
import numpy as np

# Configuration
M5OUT_DIR = "m5out"  # The root folder containing the prefetcher folders
OUTPUT_DIR = "stat_graphs" # Where to save the resulting images

# Map specific gem5 stat keys to readable names
# format: ('Readable Name', 'primary_stat_key', 'secondary_stat_key_for_division')
METRICS = [
    {
        "name": "IPC",
        "file_key": "system.cpu.ipc",
        "is_ratio": False
    },
    {
        "name": "Prefetch Accuracy",
        "file_key": "system.l2cache.prefetcher.accuracy",
        "is_ratio": False
    },
    {
        "name": "Prefetch Coverage",
        "file_key": "system.l2cache.prefetcher.coverage",
        "is_ratio": False
    },
    {
        "name": "Late Prefetches Ratio",
        "numerator": "system.l2cache.prefetcher.pfLate",
        "denominator": "system.l2cache.prefetcher.pfIssued",
        "is_ratio": True
    },
    {
        "name": "Unused Prefetches Ratio",
        "numerator": "system.l2cache.prefetcher.pfUnused",
        "denominator": "system.l2cache.prefetcher.pfIssued",
        "is_ratio": True
    },
    {
        "name": "Total DRAM Read Accesses",
        "file_key": "system.mem_ctrl.dram.numReads",
        "is_ratio": False
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

def add_average_group(data):
    """
    Calculates the arithmetic mean for each metric across all workloads 
    and adds an 'Average' entry to the data dictionary.
    """
    if not data:
        return

    # Identify all prefetchers present in the data
    # We scan all workloads to ensure we catch every prefetcher
    all_prefetchers = set()
    for w in data:
        all_prefetchers.update(data[w].keys())
    
    num_workloads = len(data)
    average_data = {p: {m['name']: 0.0 for m in METRICS} for p in all_prefetchers}

    # Sum up values
    for workload in data:
        for prefetcher in all_prefetchers:
            # If a specific prefetcher missed a workload, it defaults to 0 here
            p_data = data[workload].get(prefetcher, {})
            for metric in METRICS:
                m_name = metric['name']
                average_data[prefetcher][m_name] += p_data.get(m_name, 0.0)

    # Divide by count to get average
    for prefetcher in average_data:
        for m_name in average_data[prefetcher]:
            average_data[prefetcher][m_name] /= num_workloads

    # Add to main data structure
    data['Average'] = average_data

def plot_data(data):
    if not data:
        return

    # Create output directory
    if not os.path.exists(OUTPUT_DIR):
        os.makedirs(OUTPUT_DIR)

    # Get sorted lists for consistent plotting
    # We separate 'Average' to ensure it is appended at the very end
    workload_keys = [k for k in data.keys() if k != 'Average']
    workloads = sorted(workload_keys)
    
    # If 'Average' exists in data, add it to the end of the list
    if 'Average' in data:
        workloads.append('Average')
    
    # Find all unique prefetchers encountered
    all_prefetchers = set()
    for w in data:
        all_prefetchers.update(data[w].keys())
    prefetchers = sorted(list(all_prefetchers))

    # X-axis setup
    x = np.arange(len(workloads))
    width = 0.8 / len(prefetchers)  # Calculate bar width based on number of prefetchers

    # Generate one graph per metric
    for metric in METRICS:
        metric_name = metric["name"]
        
        fig, ax = plt.subplots(figsize=(14, 6)) # Slightly wider to accommodate Average
        
        # Create bars for each prefetcher
        for i, prefetcher in enumerate(prefetchers):
            y_values = []
            for workload in workloads:
                # Get value, default to 0 if missing for specific combo
                val = data.get(workload, {}).get(prefetcher, {}).get(metric_name, 0.0)
                y_values.append(val)
            
            # Calculate offset for grouped bars
            offset = width * i
            # Center the group around the tick
            centering = (width * len(prefetchers)) / 2
            
            # Plot the bars
            ax.bar(x + offset - centering + (width/2), y_values, width, label=prefetcher)

        # Formatting
        ax.set_xlabel('Workloads')
        ax.set_ylabel(metric_name)
        ax.set_title(f'{metric_name} by Workload (with Average)')
        ax.set_xticks(x)
        ax.set_xticklabels(workloads)
        
        # Move legend outside the plot area
        ax.legend(title="Prefetchers", bbox_to_anchor=(1.01, 1), loc='upper left')
        
        plt.tight_layout()
        
        # Save file
        filename = f"{metric_name.replace(' ', '_').lower()}.png"
        save_path = os.path.join(OUTPUT_DIR, filename)
        plt.savefig(save_path, dpi=300)
        print(f"Generated graph: {save_path}")
        plt.close()

if __name__ == "__main__":
    print(f"Scanning directory: {M5OUT_DIR}...")
    processed_data = collect_data()
    
    if processed_data:
        print(f"Found {len(processed_data)} workloads. Calculating averages...")
        add_average_group(processed_data)
        plot_data(processed_data)
        print("Done!")
