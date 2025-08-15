#!/usr/bin/env bash

DEST=tici:/home/my/openpilot/selfdrive/debug/profiling/perfetto

scp -r perfetto/out/linux/tracebox $DEST
scp -r perfetto/test/configs $DEST
