#!/bin/bash
set -x
set -e

DIR="$(dirname "$(readlink -f "$0")")"

# Set common options for our
export SAMPLE_DIR=samples/bcf-sensor
export PATCHES="${PATCHES:-} 802154-mpu.patch ieee802154_rf2xx_size_check.patch wdt_sam_watchdog_callback_check.patch"
export OVERLAYS=

export EXTRA_DEFINES="-DCONFIG_SHELL=n -DCONFIG_NET_SHELL=n -DCONFIG_NET_L2_IEEE802154_SHELL=n -DCONFIG_NET_SHELL_DYN_CMD_COMPLETION=n "

$DIR/docker_build_sample.sh
