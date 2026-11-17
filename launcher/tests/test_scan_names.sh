#!/usr/bin/env bash
# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright 2026 XxXJoker13XxX and contributors
# Name and path scanner host test (plan Task E6).
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
python3 -I "$here/test_scan_names.py"
