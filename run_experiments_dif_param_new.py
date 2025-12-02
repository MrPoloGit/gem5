import subprocess
import os
import time

# --- Configuration ---
gem5_exec = "./build/ALL/gem5.opt"
script_path = "configs/bingo_test/run_bench_solo.py"
binary_base_path = "NPB3.3.1/NPB3.3-SER/bin"
MAX_CONCURRENT = 14

# Workloads to run for every configuration
workloads = ["bt", "cg", "dc", "lu"]
#workloads = ["bt", "cg", "dc", "ft", "is", "lu", "mg", "sp"]

# --- Definition of Experiments ---

experiments = [
    # 3. Memory Constraint (System Sizing)
    # Tests MLOP effectiveness under different resource constraints.
    # Vary: Both L2 and Mem size together.
    # Note: We handle this loop manually in the task builder below because it requires changing 2 flags.
    {
        "name": "mlop_memory",
        "type": "custom_memory_sweep", # Special tag for custom logic
        "configs": [
            {"label": "abundant",    "l2": "1MB",   "mem": "512MB"}
        ],
        "flags": ["--prefetcher", "mlop"]
    }
]

def run_pool():
    # 1. Build Task Queue
    # Task Format: (experiment_name, specific_label, command_list_args)
    tasks = []

    for exp in experiments:
        if exp.get("type") == "custom_memory_sweep":
            # Handle the Memory Experiment (Multi-flag changes)
            for cfg in exp["configs"]:
                label = cfg["label"]
                base_args = exp["flags"] + ["--l2-size", cfg["l2"], "--mem-size", cfg["mem"]]
                
                for wl in workloads:
                    tasks.append({
                        "exp_name": exp["name"],
                        "sub_dir": label,
                        "workload": wl,
                        "base_args": base_args
                    })
        else:
            # Handle Standard Single-Param Sweeps
            param_flag = exp["vary_param"]
            for val in exp["values"]:
                base_args = exp["flags"] + [param_flag, val]
                
                for wl in workloads:
                    tasks.append({
                        "exp_name": exp["name"],
                        "sub_dir": val, # e.g., "16" or "500"
                        "workload": wl,
                        "base_args": base_args
                    })

    total_tasks = len(tasks)
    print(f"==================================================")
    print(f"Queued {total_tasks} MLOP experiments. Max concurrent: {MAX_CONCURRENT}")
    print(f"==================================================")

    running = []
    completed_count = 0

    # 2. Main Execution Loop
    while tasks or running:
        # --- A. Cleanup Finished Processes ---
        for i in range(len(running) - 1, -1, -1):
            task, p, log_file = running[i]
            ret = p.poll()
            
            if ret is not None:
                completed_count += 1
                log_file.close()
                status = "Success" if ret == 0 else f"FAILED ({ret})"
                print(f"  [{status}] {task['exp_name']}/{task['sub_dir']}/{task['workload']} ({completed_count}/{total_tasks})")
                del running[i]

        # --- B. Launch New Processes ---
        while len(running) < MAX_CONCURRENT and tasks:
            next_task = tasks.pop(0)
            
            # Construct File Paths
            # Directory structure: m5out/experiment_name/value/workload
            # Example: m5out/mlop_lookahead/16/bt
            outdir = f"m5out/{next_task['exp_name']}/{next_task['sub_dir']}/{next_task['workload']}"
            binary = f"{binary_base_path}/{next_task['workload']}.S.x"
            
            # Construct Command
            cmd = [
                gem5_exec,
                f"--outdir={outdir}",
                script_path
            ]
            cmd.extend(next_task['base_args'])
            cmd.append(binary)

            os.makedirs(outdir, exist_ok=True)
            
            try:
                log_file = open(f"{outdir}/console.log", "w")
                # Using subprocess to run gem5
                p = subprocess.Popen(cmd, stdout=log_file, stderr=subprocess.STDOUT)
                running.append((next_task, p, log_file))
                print(f"  [Launch] {next_task['exp_name']}/{next_task['sub_dir']}/{next_task['workload']}")
            except Exception as e:
                print(f"  [Error] Failed to launch {next_task['workload']}: {e}")

        # --- C. Idle Wait ---
        if running:
            time.sleep(0.5)

    print("\nAll MLOP experiments finished!")

if __name__ == "__main__":
    run_pool()
