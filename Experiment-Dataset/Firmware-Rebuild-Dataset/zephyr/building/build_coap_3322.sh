#!/bin/bash
DIR="$(dirname "$(readlink -f "$0")")"

export CVENUM=2021-3322-coap
export BASE_COMMIT=2a423bc6d37f916771bce65672efadf30e6ea74c
export FIX_COMMITS="2a423bc6d37f916771bce65672efadf30e6ea74c 6917d268482afc2da617a57456e1cdf4dd9c75d4"

export PATCHES="fix-CVE-2021-3323.patch"

export BOARD=sam4e_xpro
export SHIELD=atmel_rf2xx_xpro
export ZEPHYR_VERSION=2.4.0

"$DIR/docker_build_coap_sample.sh"
