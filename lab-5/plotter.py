import matplotlib.pyplot as plt
import sys

# ---- X-axis values (edit these if your sweep changes) ----
LENGTH_X = [8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384, 32768, 65536, 131072, 262144, 524288]
NODES_X = [2, 3, 4, 5, 6, 7, 8, 9, 10]
PROCS_X = [1, 2, 3, 4]


def read_times(filepath):
    algo_times = []
    mpi_times = []
    with open(filepath, "r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            parts = line.split(",")
            if len(parts) < 2:
                continue
            algo_times.append(float(parts[0]))
            mpi_times.append(float(parts[1]))
    return algo_times, mpi_times


def plot(x, algo_y, mpi_y, xlabel, ylabel="Time (s)", title="", outfile=None, log_axes=False):
    plt.figure()
    plt.plot(x, algo_y, marker="o", color="blue", label="Algorithm 1")
    plt.plot(x, mpi_y, marker="s", color="red", label="Algorithm 2")
    if log_axes:
        plt.xscale("log", base=2)
        plt.yscale("log")
    plt.xlabel(xlabel)
    plt.ylabel(ylabel)
    plt.legend()
    if title:
        plt.title(title)
    plt.grid(True)
    plt.tight_layout()
    if outfile:
        plt.savefig(outfile, dpi=150)
        print(f"Saved {outfile}")
    else:
        plt.show()


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python plotter.py <file> [length|nodes|processes] [output.png]")
        sys.exit(1)

    filepath = sys.argv[1]
    category = sys.argv[2] if len(sys.argv) >= 3 else "length"
    outfile = sys.argv[3] if len(sys.argv) >= 4 else None

    x_map = {"length": LENGTH_X, "nodes": NODES_X, "processes": PROCS_X}
    label_map = {"length": "Message Size (bytes)", "nodes": "Nodes", "processes": "Processes per Node"}

    x = x_map.get(category, LENGTH_X)
    xlabel = label_map.get(category, category)
    algo_y, mpi_y = read_times(filepath)

    n = len(algo_y)
    plot(x[:n], algo_y, mpi_y, xlabel, title=f"{category} sweep", outfile=outfile, log_axes=(category == "length"))
