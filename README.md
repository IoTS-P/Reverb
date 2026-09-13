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
#### A.1 Environment Setup
#### A.2 Collecting Device Inputs and Snapshots
#### A.3 Recording Outputs

### B. Rewinding
#### B.1 Environment Setup
#### B.2 Rewinding with Recorded Inputs
#### B.3 Reconstructed Execution Trace

## Issues
If you encounter any problems while using our tool, please open an issue. 

For other communications, you can email `shenshandian@hust.edu.cn` and `ke_wang_tt@hust.edu.cn`
