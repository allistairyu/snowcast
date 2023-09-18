#!/bin/bash

set -euo pipefail

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )

TARGET=~/.config/wireshark/plugins

main() {
    mkdir -pv "${TARGET}"
    install -v -m644 "${SCRIPT_DIR}/cs168_snowcast.lua" "${TARGET}"
}

main $@
