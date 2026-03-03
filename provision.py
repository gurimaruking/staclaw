#!/usr/bin/env python3
"""
staclaw NVS provisioning tool.
Generates NVS partition binary and flashes it to M5Stack Core2.

Usage:
    python provision.py                   # Interactive mode
    python provision.py --csv config.csv  # From CSV file
    python provision.py --flash           # Generate + flash to device
"""

import argparse
import csv
import os
import subprocess
import sys
import tempfile

IDF_PATH = os.environ.get("IDF_PATH", r"C:\Espressif\frameworks\esp-idf-v5.5.2-2")
PYTHON = os.environ.get("IDF_PYTHON", r"C:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe")
NVS_TOOL = os.path.join(IDF_PATH, "components", "nvs_flash", "nvs_partition_gen", "nvs_partition_gen.py")
ESPTOOL = os.path.join(IDF_PATH, "components", "esptool_py", "esptool", "esptool.py")

NVS_SIZE = 0x6000  # 24KB, must match partitions.csv
NVS_OFFSET = 0x9000

NAMESPACE = "staclaw"

# NVS key -> (prompt, type, required)
FIELDS = [
    ("wifi_ssid",  "WiFi SSID",               "string", True),
    ("wifi_pass",  "WiFi Password",            "string", True),
    ("claude_key", "Claude API Key (sk-ant-...)", "string", False),
    ("openai_key", "OpenAI API Key (sk-...)",  "string", False),
    ("tg_token",   "Telegram Bot Token",       "string", False),
    ("active_llm", "Active LLM (claude/openai)", "string", False),
]


def interactive_input():
    """Prompt user for config values."""
    print("\n=== staclaw NVS Provisioning ===\n")
    values = {}
    for key, prompt, dtype, required in FIELDS:
        suffix = " (required)" if required else " (optional, Enter to skip)"
        val = input(f"  {prompt}{suffix}: ").strip()
        if val:
            values[key] = val
        elif required:
            print(f"  ERROR: {prompt} is required!")
            sys.exit(1)
    return values


def write_nvs_csv(values, path):
    """Write NVS CSV file for nvs_partition_gen.py"""
    with open(path, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["key", "type", "encoding", "value"])
        w.writerow([NAMESPACE, "namespace", "", ""])
        for key, val in values.items():
            w.writerow([key, "data", "string", val])
    print(f"  NVS CSV written: {path}")


def generate_bin(csv_path, bin_path):
    """Generate NVS binary from CSV."""
    cmd = [PYTHON, NVS_TOOL, "generate", csv_path, bin_path, hex(NVS_SIZE)]
    print(f"  Generating NVS binary...")
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        print(f"  ERROR: {r.stderr}")
        sys.exit(1)
    print(f"  NVS binary: {bin_path} ({os.path.getsize(bin_path)} bytes)")


def flash_nvs(bin_path, port="COM4"):
    """Flash NVS binary to device."""
    esptool_path = os.path.join(IDF_PATH, "components", "esptool_py", "esptool")
    cmd = [
        PYTHON, "-m", "esptool",
        "--chip", "esp32",
        "-p", port,
        "-b", "460800",
        "write_flash", hex(NVS_OFFSET), bin_path,
    ]
    env = os.environ.copy()
    env["PYTHONPATH"] = esptool_path + os.pathsep + env.get("PYTHONPATH", "")
    print(f"  Flashing NVS to {port} at offset {hex(NVS_OFFSET)}...")
    r = subprocess.run(cmd, capture_output=True, text=True, env=env)
    if r.returncode != 0:
        print(f"  ERROR: {r.stderr}")
        # Try alternative approach
        cmd2 = [PYTHON, ESPTOOL, "--chip", "esp32", "-p", port, "-b", "460800",
                "write_flash", hex(NVS_OFFSET), bin_path]
        r = subprocess.run(cmd2, capture_output=True, text=True)
        if r.returncode != 0:
            print(f"  ERROR: {r.stderr}")
            sys.exit(1)
    print("  NVS flashed successfully!")
    print("  Reset the device to apply new configuration.")


def main():
    parser = argparse.ArgumentParser(description="staclaw NVS provisioning")
    parser.add_argument("--csv", help="Input CSV file with config values")
    parser.add_argument("--flash", action="store_true", help="Flash NVS to device")
    parser.add_argument("--port", default="COM4", help="Serial port (default: COM4)")
    parser.add_argument("--output", default="nvs_config.bin", help="Output binary path")
    args = parser.parse_args()

    if args.csv:
        # Read from existing CSV
        values = {}
        with open(args.csv) as f:
            reader = csv.DictReader(f)
            for row in reader:
                if row.get("type") == "data":
                    values[row["key"]] = row["value"]
    else:
        values = interactive_input()

    if not values:
        print("No values provided, exiting.")
        sys.exit(1)

    # Write CSV and generate binary
    with tempfile.NamedTemporaryFile(mode="w", suffix=".csv", delete=False) as tmp:
        tmp_csv = tmp.name
    write_nvs_csv(values, tmp_csv)
    generate_bin(tmp_csv, args.output)
    os.unlink(tmp_csv)

    if args.flash:
        flash_nvs(args.output, args.port)

    print("\nDone! NVS binary saved to:", args.output)
    if not args.flash:
        print(f"To flash manually: build.bat flash_nvs (or use --flash flag)")


if __name__ == "__main__":
    main()
