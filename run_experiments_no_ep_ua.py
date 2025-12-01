import subprocess
import os
import sys

# Configuration
gem5_exec = "./build/ALL/gem5.opt"
script_path = "configs/bingo_test/run_bench.py"
binary_base_path = "NPB3.3.1/NPB3.3-SER/bin"

prefetchers = ["bingo", "stride", "mlop", "bop"]
workloads = ["bt", "cg", "dc", "ft", "is", "lu", "mg", "sp"]

def run_batch():
    # 1. Loop through prefetchers sequentially
    for pref in prefetchers:
        print(f"==================================================")
        print(f"Starting batch for prefetcher: {pref}")
        print(f"==================================================")
        
        processes = []

        # 2. Launch all workloads in parallel for this prefetcher
        for wl in workloads:
            # Construct paths
            binary = f"{binary_base_path}/{wl}.S.x"
            outdir = f"m5out/{pref}/{wl}"
            
            # Construct the command
            cmd = [
                gem5_exec,
                f"--outdir={outdir}",
                script_path,
                "--prefetcher", pref,
                binary
            ]
            
            # Create output directory manually to ensure log file path exists
            os.makedirs(outdir, exist_ok=True)
            
            print(f"  [Launching] {wl}...")
            
            # Open a log file for this simulation to prevent console clutter
            with open(f"{outdir}/console.log", "w") as log_file:
                # subprocess.Popen launches background process
                p = subprocess.Popen(cmd, stdout=log_file, stderr=subprocess.STDOUT)
                processes.append((wl, p))

        print(f"\n--- All 10 workloads running for {pref}. Waiting... ---\n")

        # 3. Wait for all 10 workloads to finish
        for wl, p in processes:
            p.wait()
            if p.returncode == 0:
                print(f"  [Success] {wl} finished.")
            else:
                print(f"  [FAILED]  {wl} exited with code {p.returncode}. Check logs.")

        print(f"\nCompleted batch for {pref}.\n")

    print("All experiments finished!")

if __name__ == "__main__":
    run_batch()
