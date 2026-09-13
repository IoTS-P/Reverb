#!/bin/bash
DIR="$(dirname "$(readlink -f "$0")")"

export CVENUM=mpu
export BASE_COMMIT=e1dddf7befa7309bd2afc567b2e00d2e7362f7c4
export FIX_COMMITS=

export ZEPHYR_VERSION=2.2.0

export BOARD="nrf52840dk_nrf52840"

# Fix the buffer OOB issue and prepare bt hostonly build
export PATCHES="mpu.patch fix-CVE-2020-10065.patch bt_hci_cmd_timeout.patch bt_hostonly_log.patch"

DIR="$(dirname "$(readlink -f "$0")")"

export SAMPLE_DIR=samples/mpu/mpu_test


$DIR/docker_build_sample.sh
