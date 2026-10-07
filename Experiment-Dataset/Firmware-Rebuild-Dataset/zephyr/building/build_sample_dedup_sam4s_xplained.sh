#!/bin/bash
DIR="$(dirname "$(readlink -f "$0")")"

export CVENUM=2021-3319-30
export BASE_COMMIT=40aab3276c0d0d419c665c184f0c8eeb00de2eb0
export FIX_COMMITS=

# export PATCHES="fix-CVE-2021-3323.patch"

export BOARD=sam4s_xplained
export SHIELD=atmel_rf2xx_xplained
export ZEPHYR_VERSION=2.4.0

"$DIR/docker_build_802154_sample.sh"
