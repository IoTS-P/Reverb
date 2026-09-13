#!/bin/bash
DIR="$(dirname "$(readlink -f "$0")")"

export CVENUM=2021-3319-30-coap
export BASE_COMMIT=40aab3276c0d0d419c665c184f0c8eeb00de2eb0
export FIX_COMMITS=

export BOARD=sam4e_xpro
export SHIELD=atmel_rf2xx_xpro
export ZEPHYR_VERSION=2.4.0

"$DIR/docker_build_coap_sample.sh"
