# Experiment

This directory holds the experiment data for the Reverb study.

## Layout

- `Firmware-Rebuild-Dataset/` — firmware rebuild materials preserved from the
  original artifact, including RIOT and Zephyr build scripts, patches, sample
  sources/references, and prebuilt reference images.

- All other directories are individual experiment cases, one per firmware /
  device:

  | Directory | Directory | Directory |
  | --- | --- | --- |
  | `ENV-Sensor/` | `Fan-Controller/` | `Gateway/` |
  | `Heat-Press/` | `Light-Sensor/` | `Motion-Sensor/` |
  | `PLC/` | `Smart-Light/` | `Smart-Lock/` |
  | `Smart-Switch/` | `Thermostat/` | |

## What each case directory contains

Each case directory is self-contained and documents, inside the folder itself:

1. **How the data was collected** (collection scripts, input seeds, `.in`
   files, `.crash_memshot` files, configuration of the harness), or the
   **collection results** when the raw artifacts are kept instead
   (`.log`, `.csv`, `.mem` dumps, etc.).
2. **The real-device path** — the traces / logs obtained on the physical
   device (files suffixed `_real`, `real_*` or similar).
3. **The simulation path** — the traces / logs obtained under the emulator
   / simulator (files suffixed `_sim`, `sim_*` or similar).

Some cases are organized as sub-cases named after the vulnerability they
target (e.g. `ENV-Sensor/CVE-2020-10069/`, `Smart-Lock/CVE-2021-3329/`,
`Smart-Lock/CVE-2023-0397/`), with each sub-case holding its own collection
setup, real-device path and simulation path.

## Notes

- Comparison scripts (`compare.py`, `all_compare.py`, `*_compare.py`) diff the
  real-device and simulation paths to measure fidelity.
- `Firmware-Rebuild-Dataset/zephyr/rebuild_targets.sh` rebuilds the Zephyr firmware targets.
