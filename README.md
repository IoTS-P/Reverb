# Reverb

Reverb is an **ex-vivo failure execution reconstruction and diagnosis framework for MCU-based IoT devices in real-world deployments**.

Unlike conventional firmware debugging and emulation approaches that require runtime instrumentation, hardware tracing support, or accurate peripheral models, Reverb leverages the observation that most external interactions of IoT devices pass through a centralized edge device.

Reverb consists of two major components:

- **Recording:** Reverb continuously records edge-observable device inputs at the IoT gateway/edge and maintains a recent device snapshot, without introducing runtime overhead to the resource-constrained IoT device.
- **Rewinding:** When a failure occurs, Reverb restores the firmware from the recorded snapshot and uses the recorded inputs to guide cloud-side firmware execution. For device-local and nondeterministic inputs that are invisible to the edge, Reverb automatically infers the missing values and reconstructs the faulty execution trace.

Reverb enables faithful reproduction of failures observed on deployed IoT devices and provides reconstructed execution traces for downstream tasks such as *postmortem analysis and root-cause diagnosis*.



## Citing Our Paper

If you use Reverb in your research, please cite our paper:

```bibtex
@article{lu2026reverb,
  author    = {Ruibo Lu and Wei Zhou and Shandian Shen and Ke Wang and Yuqing Zhang},
  title     = {Reverb: Ex-Vivo Rewinding IoT Device Faulty Execution Trace with Edge-Side Recording},
  journal   = {ACM Transactions on Software Engineering and Methodology},
  year      = {2026},
  publisher = {Association for Computing Machinery},
  doi       = {10.1145/3838730},
  url       = {https://doi.org/10.1145/3838730}
}

```

## Directory Structure
```bash

Reverb/
├── Reverb-Record-Component/                 # Edge-side input recording
│
├── Reverb-Rewind-Component/                 # Cloud-side execution rewinding
│   ├── emulator/              # Firmware execution environment
│   ├── plugins/               # Reverb analysis/replay plugins
│   ├── scripts/               # Rewinding and analysis scripts
│   └── examples/              # Example replay configurations
│
├── Script/                 
├── Experiment-Dataset/
├── Doc/                       
├── LICENSE
└── README.md

```
## License

Content of this repository is licensed under GPL-3.0. See [LICENSE](./LICENSE).

## Usage

### A. Recording

The recording-side artifacts are stored in [`Reverb-Record-Component/`](./Reverb-Record-Component/). Its `record/` directory is preserved from the original RnR-AE artifact and provides the MQTT-SN recording setup used there. It is not an exhaustive collection of all Reverb recording mechanisms.

#### A.1 Environment Setup

The recording setup uses the Eclipse Mosquitto RSMB broker with MQTT-SN support and contains:

```text
Reverb-Record-Component/record/
├── broker.sh
├── mosquitto.rsmb
└── mqtt_broker_config.conf
```

The bundled `mosquitto.rsmb` refers to commit `36fd4ba9433da172f0af580eb6c1a3139b63c355`.

The provided [`mqtt_broker_config.conf`](./Reverb-Record-Component/record/mqtt_broker_config.conf) enables protocol tracing, with an MQTT-SN listener on UDP port `1885` and an MQTT listener on TCP port `1886`. It also produces protocol-level trace output for traffic observed by the broker.

#### A.2 Collecting Device Inputs and Snapshots

This MQTT-SN broker setup represents only one part of Reverb's recording artifacts. Other experiment-specific artifacts, such as snapshots and hardware/input records, are stored alongside the corresponding cases in [`Experiment-Dataset/`](./Experiment-Dataset/).

#### A.3 Recording Outputs

The supplied broker configuration supports protocol traces. Individual experiment cases also preserve collection scripts, input seeds, `.in` files, `.crash_memshot` files, and/or recorded `.log`, `.csv`, and `.mem` outputs where available; see the relevant case directory.

### B. Rewinding

After recording at the edge, Reverb uses the recorded inputs and device snapshots to guide firmware execution in the cloud (or on a local server). It automatically infers the missing nondeterministic inputs to reconstruct the complete faulty execution trace.

#### B.1 Environment Setup

1. **Install Base Emulator (uEmu)**: Follow the instructions in the official [MCUSec/uEmu repository](https://github.com/MCUSec/uEmu) to complete the basic installation and dependency configuration of the uEmu framework.
2. **Replace Core Component (Reverb-Rewind)**: Replace the default S2E engine component in uEmu with the `Reverb-Rewind-Component` module provided in this repository.
3. **Recompile the Environment**: After replacing the components, recompile the entire project in your uEmu directory to apply the Reverb analysis engine and plugins.
4. **Install Ghidra (Version 10.3)**: Download and install Ghidra 10.3. Reverb utilizes Ghidra's headless mode for auxiliary static firmware analysis.

#### B.2 Rewinding with Recorded Inputs

Before starting the rewinding process, you need to prepare the configuration files and the corresponding input files, placing them in the appropriate directories within your workspace.

1. **Generate Firmware Configuration File**: Create the corresponding S2E/uEmu configuration file for the target firmware. 
   *💡 Reference: You can look at `Experiment-Dataset/Motion-Sensor/motion_sensor-config.lua` to understand how to configure parameters such as memory mapping, interrupts, and symbolic input points.*
2. **Prepare Input Files**: Ensure you have the recorded input files required for firmware execution (i.e., the `.in` file and the device snapshot file).
3. **Launch Ghidra for Static Analysis**: Use the provided `scripts/launch-ghidra.sh` script to launch Ghidra. This script uses `scripts/ControlFlowGraph.java` to extract the Control Flow Graph (CFG) information of the firmware, which provides essential support for the execution reconstruction.
4. **Start S2E for Rewinding**: Use the `scripts/launch-motion_sensor.sh` script (or adapt it for your specific firmware) to load the firmware and configuration, and start dynamic rewinding.

#### B.3 Reconstructed Execution Trace

The S2E engine combines the recorded external inputs with symbolic execution to faithfully follow the real execution path. Once it reaches the point of failure, the crash is successfully reproduced. This provides a detailed execution context and a fully reconstructed execution trace for downstream tasks such as postmortem analysis and root-cause diagnosis.

## Experiment Dataset

The [`Experiment-Dataset/`](./Experiment-Dataset/) directory contains the experiment data for the Reverb study.

### Layout

- [`Firmware-Rebuild-Dataset/`](./Experiment-Dataset/Firmware-Rebuild-Dataset/) preserves firmware rebuild materials from the original artifact: RIOT and Zephyr build scripts, patches, sample sources/references, and prebuilt reference images.
- The other directories contain individual firmware/device experiment cases: `ENV-Sensor/`, `Fan-Controller/`, `Gateway/`, `Heat-Press/`, `Light-Sensor/`, `Motion-Sensor/`, `PLC/`, `Smart-Light/`, `Smart-Lock/`, `Smart-Switch/`, and `Thermostat/`.

### What Each Case Directory Contains

Each case stores or documents the following within its own directory:

1. **Collection setup or outputs:** collection scripts, input seeds, `.in` files, `.crash_memshot` files, harness configuration, or saved `.log`, `.csv`, and `.mem` results.
2. **Real-device path:** physical-device traces or logs, often named with `_real`, `real_*`, or similar suffixes/prefixes.
3. **Simulation path:** emulator/simulator traces or logs, often named with `_sim`, `sim_*`, or similar suffixes/prefixes.

Some cases are organized into vulnerability-specific subdirectories, such as `ENV-Sensor/CVE-2020-10069/`, `Smart-Lock/CVE-2021-3329/`, and `Smart-Lock/CVE-2023-0397/`. Each sub-case contains its corresponding collection setup, real-device path, and simulated path.

### Notes

- Comparison scripts (`compare.py`, `all_compare.py`, and `*_compare.py`) compare real-device and simulated execution paths to measure fidelity.
- [`Firmware-Rebuild-Dataset/zephyr/rebuild_targets.sh`](./Experiment-Dataset/Firmware-Rebuild-Dataset/zephyr/rebuild_targets.sh) rebuilds Zephyr firmware targets.

## Issues
If you encounter any problems while using our tool, please open an issue. 

For other communications, you can email `shenshandian@hust.edu.cn` and `ke_wang_tt@hust.edu.cn`
