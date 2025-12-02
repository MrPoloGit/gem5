import subprocess
import os
import time

# Configuration
gem5_exec = "./build/ALL/gem5.opt"
script_path = "configs/bingo_test/run_bench.py"
binary_base_path = "NPB3.3.1/NPB3.3-SER/bin"

# Bingo Specific Configuration
BINGO_REGION_SIZE = "4096"
BINGO_ACC_ENTRIES = "64"
BINGO_HIST_ENTRIES = "12288"
BINGO_HIST_ASSOC = "16"

# Limits
MAX_CONCURRENT = 16  # <--- Change this to your desired max value

prefetchers = ["mlop"]
workloads = ["bt", "cg", "dc", "ft", "is", "lu", "mg", "sp"]

def run_pool():
    # 1. Create a queue of all tasks (flatten the loops)
    # Each task is a tuple: (prefetcher_name, workload_name)
    tasks = [(pref, wl) for pref in prefetchers for wl in workloads]
    
    total_tasks = len(tasks)
    print(f"==================================================")
    print(f"Queued {total_tasks} experiments. Max concurrent: {MAX_CONCURRENT}")
    print(f"==================================================")

    # List to track running processes: [(pref, wl, process_obj, log_file_handle)]
    running = []
    completed_count = 0

    # 2. Main Loop: continue while there are tasks left to start OR tasks currently running
    while tasks or running:
        # --- A. Check for finished processes ---
        # We iterate in reverse to safely remove items while looping
        for i in range(len(running) - 1, -1, -1):
            pref, wl, p, log_file = running[i]
            
            # poll() returns None if running, or the exit code if finished
            ret = p.poll() 
            
            if ret is not None:
                # Process finished
                completed_count += 1
                log_file.close() # Good practice to close the file handle
                
                if ret == 0:
                    print(f"  [Success] {pref}/{wl} finished. ({completed_count}/{total_tasks})")
                else:
                    print(f"  [FAILED]  {pref}/{wl} exited with code {ret}. Check logs.")
                
                # Remove from running list
                del running[i]

        # --- B. Fill empty slots if we have tasks waiting ---
        while len(running) < MAX_CONCURRENT and tasks:
            next_pref, next_wl = tasks.pop(0)
            
            # Construct paths
            binary = f"{binary_base_path}/{next_wl}.S.x"
            outdir = f"m5out/{next_pref}/{next_wl}"
            
            # Build the base command
            cmd = [
                gem5_exec,
                f"--outdir={outdir}",
                script_path,
                "--prefetcher", next_pref,
            ]
            
            # Add specific flags if the prefetcher is Bingo
            if next_pref == "bingo":
                cmd.extend([
                    "--bingo-region-size", BINGO_REGION_SIZE,
                    "--bingo-acc-entries", BINGO_ACC_ENTRIES,
                    "--bingo-hist-entries", BINGO_HIST_ENTRIES,
                    "--bingo-hist-assoc", BINGO_HIST_ASSOC
                ])

            # Append binary at the end
            cmd.append(binary)
            
            os.makedirs(outdir, exist_ok=True)
            
            print(f"  [Launching] {next_pref}/{next_wl}...")
            
            # Open log file
            try:
                log_file = open(f"{outdir}/console.log", "w")
                p = subprocess.Popen(cmd, stdout=log_file, stderr=subprocess.STDOUT)
                
                # Add to running list
                running.append((next_pref, next_wl, p, log_file))
            except Exception as e:
                print(f"  [Error] Failed to launch {next_pref}/{next_wl}: {e}")

        # --- C. Wait a bit before checking again to prevent CPU spinning ---
        if running:
            time.sleep(1)

    print("\nAll experiments finished!")

if __name__ == "__main__":
    run_pool()
