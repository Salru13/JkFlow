#!/usr/bin/env bash
# Copyright (c) 2026 XxXJoker13XxX. All rights reserved.
# Name and path scanner host test (plan Task E6).
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
python3 -I "$here/test_scan_names.py"
