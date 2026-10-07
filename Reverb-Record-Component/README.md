# Reverb Record Component

This directory contains artifacts related to the recording side of Reverb.

## `record/`

The `record/` directory is preserved from the original RnR-AE artifact. It contains the MQTT-SN recording setup used in the artifact:

```text
record/
├── broker.sh
├── mosquitto.rsmb
└── mqtt_broker_config.conf
```

The setup uses the Eclipse Mosquitto RSMB broker with MQTT-SN support. The supplied configuration enables protocol tracing and provides:

- an MQTT-SN listener on UDP port `1885`;
- an MQTT listener on TCP port `1886`;
- protocol-level trace output for traffic observed by the broker.

`mosquitto.rsmb` refers to commit:

```text
36fd4ba9433da172f0af580eb6c1a3139b63c355
```

This directory represents the MQTT-SN recording setup included in the original artifact. It should not be interpreted as an exhaustive collection of all recording mechanisms used by Reverb. Other experiment-specific recording artifacts, such as snapshots and hardware/input records, are stored with their corresponding experiment data.
