#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Fixture that never reports READY (exercises the harness timeout)."""
import time

time.sleep(60)
