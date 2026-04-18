#!/bin/bash
# Wrapper that runs clang build and writes exit code to build.done
# Stdout is filtered to only emit events worth notifying about.
cd ~/src/clang-p2996
rm -f build.log build.done
set -o pipefail
ninja -C build clang 2>&1 | tee build.log | stdbuf -oL awk '
  /FAILED|^error:|ninja: error|Killed|OOM|Linking CXX.*\/clang$|build stopped/ { print; fflush() }
'
exit_code=${PIPESTATUS[0]}
echo "$exit_code" > build.done
echo "BUILD_EXIT=$exit_code"
