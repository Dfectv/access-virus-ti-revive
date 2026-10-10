#!/usr/bin/env bash
# ts.sh FILE [tshark args...] - tshark on a capture under $HOME.
# Ubuntu's AppArmor profile for /usr/bin/tshark only allows reading files in /tmp, so pipe via cat.
set -euo pipefail
F=${1:?usage: ts.sh FILE [tshark args]}; shift
cat "$F" | tshark -r - "$@"
