import os
import matplotlib.pyplot as plt
import numpy as np
import math

# Configuration
M5OUT_DIR = "m5out"
OUTPUT_DIR = "param_graphs"

# Metrics to extract
METRICS = [
    {
        "name": "IPC", 
        "file_key": "system.cpu.ipc", 
        "ylim": (0.242, 0.246)  # Set IPC Scale
    },
    {
        "name": "Prefetch Accuracy", 
        "file_key": "system.l2cache.prefetcher.accuracy", 
        "ylim": (0.65, 0.85)    # Set Accuracy Scale
    },
    {
        "name": "Prefetch Coverage", 
        "file_key": "system.l2cache.prefetcher.coverage", 
        "ylim": (0.75, 0.95)    # Set Coverage Scale
    },
]

# Experiment Definitions
EXPERIMENTS = {
    "mlop_lookahead": {
        "title": "MLOP Lookahead",
        "xlabel": "Lookahead Depth (Levels)",
        "sort_key": int,
        "description": "Deeper lookahead vs Metric"
    },
    "mlop_window": {
        "title": "MLOP Evaluation Window",
        "xlabel": "Evaluation Period (Accesses)",
        "sort_key": int,
        "description": "Window size vs Metric"
    },
    "mlop_confidence": {
        "title": "MLOP Confidence Threshold",
        "xlabel": "Score Threshold",
        "sort_key": int,
        "description": "Filtering strength vs Metric"
    },
    "mlop_memory": {
        "title": "MLOP Memory Constraints",
        "xlabel": "L2 Cache size",
        "custom_order": ["constrained", "baseline", "abundant"], # Enforce order
        "label_map": {                                           # Rename for graph
            "constrained": "128kB",
            "baseline": "256kB",
            "abundant": "1mB"
        },
        "description": "Resource availability vs Metric"
    }
}

def parse_stats_file(filepath):
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

def calculate_geomean(values):
    """Calculates geometric mean of a list of values."""
    a = np.array(values)
    # Handle zeros for geomean (replace with small epsilon or filter)
    a = a[a > 1e-6] 
    if len(a) == 0:
        return 0.0
    return np.exp(np.mean(np.log(a)))

def collect_data():
    data = {}
    if not os.path.exists(M5OUT_DIR):
        print(f"Error: {M5OUT_DIR} not found.")
        return None

    for exp_name in os.listdir(M5OUT_DIR):
        exp_path = os.path.join(M5OUT_DIR, exp_name)
        if not os.path.isdir(exp_path) or exp_name not in EXPERIMENTS:
            continue
            
        data[exp_name] = {}
        for param_val in os.listdir(exp_path):
            param_path = os.path.join(exp_path, param_val)
            if not os.path.isdir(param_path):
                continue
            
            data[exp_name][param_val] = {}
            for workload in os.listdir(param_path):
                workload_path = os.path.join(param_path, workload)
                stats_file = os.path.join(workload_path, "stats.txt")
                raw_stats = parse_stats_file(stats_file)
                
                data[exp_name][param_val][workload] = {}
                for m in METRICS:
                    val = raw_stats.get(m["file_key"], 0.0)
                    data[exp_name][param_val][workload][m["name"]] = val
    return data

def aggregate_averages(data):
    """Adds an 'Average' (Geomean) workload to the data structure for ALL metrics."""
    metric_names = [m["name"] for m in METRICS]
    
    for exp in data:
        for param in data[exp]:
            if "Average" not in data[exp][param]:
                data[exp][param]["Average"] = {}

            for m_name in metric_names:
                vals = []
                for wl in data[exp][param]:
                    if wl == "Average": continue
                    val = data[exp][param][wl].get(m_name, 0.0)
                    vals.append(val)
                
                data[exp][param]["Average"][m_name] = calculate_geomean(vals)

def plot_summary_grid(summary_data, metric_name):
    """
    Plots a 2x2 grid of Average lines for the given metric.
    summary_data: { exp_name: {param_val: avg_value, ...}, ... }
    """
    metric_conf = next((m for m in METRICS if m["name"] == metric_name), None)

    exp_names = list(summary_data.keys())
    num_exps = len(exp_names)
    cols = 2
    rows = math.ceil(num_exps / cols)
    
    fig, axes = plt.subplots(rows, cols, figsize=(14, 5 * rows))
    fig.suptitle(f"Combined Trends: {metric_name}", fontsize=16)
    
    if num_exps == 1:
        axes_flat = [axes]
    else:
        axes_flat = axes.flatten()
    
    for i, exp_name in enumerate(exp_names):
        ax = axes_flat[i]
        meta = EXPERIMENTS[exp_name]
        data_points = summary_data[exp_name]
        
        # Sort keys
        x_raw = list(data_points.keys())
        if "custom_order" in meta:
            order_map = {val: idx for idx, val in enumerate(meta["custom_order"])}
            x_sorted = sorted(x_raw, key=lambda x: order_map.get(x, 99))
        else:
            try:
                x_sorted = sorted(x_raw, key=meta["sort_key"])
            except:
                x_sorted = sorted(x_raw)

        y_sorted = [data_points[x] for x in x_sorted]

        # Apply Label Mapping if it exists (e.g. for mlop_memory)
        if "label_map" in meta:
            x_display = [meta["label_map"].get(x, x) for x in x_sorted]
        else:
            x_display = x_sorted
        
        # Plot
        if "custom_order" in meta:
             ax.plot(x_display, y_sorted, marker='s', color='black', linewidth=2, label="Average")
        else:
             x_ints = [int(x) for x in x_sorted]
             ax.plot(x_ints, y_sorted, marker='s', color='black', linewidth=2, label="Average")
             ax.set_xticks(x_ints)

        # APPLY Y-AXIS SCALING IF DEFINED (ONLY FOR SUMMARY GRAPHS)
        if metric_conf and "ylim" in metric_conf:
            ax.set_ylim(metric_conf["ylim"])

        ax.set_title(meta["title"])
        ax.set_xlabel(meta["xlabel"])
        ax.set_ylabel(metric_name)
        ax.grid(True, linestyle='--', alpha=0.5)

    # Hide unused subplots
    for j in range(i + 1, len(axes_flat)):
        axes_flat[j].axis('off')
        
    plt.tight_layout(rect=[0, 0.03, 1, 0.95])
    save_path = os.path.join(OUTPUT_DIR, f"Summary_{metric_name.replace(' ', '_')}.png")
    plt.savefig(save_path, dpi=300)
    plt.close()
    print(f"  [Combined] Saved {save_path}")

def plot_experiments(data):
    if not os.path.exists(OUTPUT_DIR):
        os.makedirs(OUTPUT_DIR)
    
    metric_names = [m["name"] for m in METRICS]
    cmap = plt.get_cmap("tab10")

    # Store averages for the summary plot
    summary_store = {m: {} for m in metric_names}

    for m_name in metric_names:
        print(f"\n--- Plotting Metric: {m_name} ---")
        
        for exp_name, params_data in data.items():
            if exp_name not in summary_store[m_name]:
                summary_store[m_name][exp_name] = {}
            
            meta = EXPERIMENTS[exp_name]
            
            # Sort X-Axis
            x_values_raw = list(params_data.keys())
            if "custom_order" in meta:
                order_map = {val: i for i, val in enumerate(meta["custom_order"])}
                x_values_sorted = sorted(x_values_raw, key=lambda x: order_map.get(x, 99))
            else:
                try:
                    x_values_sorted = sorted(x_values_raw, key=meta["sort_key"])
                except:
                    x_values_sorted = sorted(x_values_raw)

            # Apply Label Mapping if it exists
            if "label_map" in meta:
                x_display = [meta["label_map"].get(x, x) for x in x_values_sorted]
            else:
                x_display = x_values_sorted

            # Collect Y-Axis Data
            all_workloads = set()
            for p in params_data:
                all_workloads.update(params_data[p].keys())
            if "Average" in all_workloads:
                all_workloads.remove("Average")
            sorted_workloads = sorted(list(all_workloads))

            fig, ax = plt.subplots(figsize=(10, 6))
            
            # Plot Individual Workloads
            for i, wl in enumerate(sorted_workloads):
                y_points = []
                for x in x_values_sorted:
                    val = params_data[x].get(wl, {}).get(m_name, None)
                    y_points.append(val)
                
                if "custom_order" in meta:
                    # Use x_display here for the mapped labels
                    ax.plot(x_display, y_points, marker='o', alpha=0.4, linewidth=1, label=wl, color=cmap(i % 10))
                else:
                    x_ints = [int(x) for x in x_values_sorted]
                    ax.plot(x_ints, y_points, marker='o', alpha=0.4, linewidth=1, label=wl, color=cmap(i % 10))

            # Plot Average Line
            avg_points = []
            for x in x_values_sorted:
                val = params_data[x].get("Average", {}).get(m_name, 0)
                avg_points.append(val)
                summary_store[m_name][exp_name][x] = val # Store for summary
                
            if "custom_order" in meta:
                ax.plot(x_display, avg_points, marker='s', color='black', linewidth=3, label='Average (Geomean)')
            else:
                x_ints = [int(x) for x in x_values_sorted]
                ax.plot(x_ints, avg_points, marker='s', color='black', linewidth=3, label='Average (Geomean)')
                ax.set_xticks(x_ints)

            # NOTE: Y-AXIS SCALING IS NOT APPLIED HERE (as requested)

            ax.set_title(f"{meta['title']} - {m_name}")
            ax.set_xlabel(meta["xlabel"])
            ax.set_ylabel(m_name)
            ax.grid(True, linestyle='--', alpha=0.5)
            ax.legend(bbox_to_anchor=(1.05, 1), loc='upper left', borderaxespad=0.)
            
            plt.tight_layout()
            filename = os.path.join(OUTPUT_DIR, f"{exp_name}_{m_name.replace(' ', '_')}.png")
            plt.savefig(filename, dpi=300)
            plt.close()
            print(f"  Saved {filename}")

        # After finishing all experiments for this metric, plot the summary
        plot_summary_grid(summary_store[m_name], m_name)

if __name__ == "__main__":
    print("Collecting data...")
    data = collect_data()
    
    if data:
        print("Calculating averages...")
        aggregate_averages(data)
        
        print("Generating Graphs...")
        plot_experiments(data)
        print("Done! Check the 'param_graphs' folder.")
    else:
        print("No data found. Please run run_experiments_dif_param.py first.")
