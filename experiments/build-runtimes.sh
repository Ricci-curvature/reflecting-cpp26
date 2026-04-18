#!/bin/bash
# Build libcxx/libcxxabi/libunwind runtimes. Emits only noteworthy events.
cd ~/src/clang-p2996
rm -f build-runtimes.log build-runtimes.done
set -o pipefail
ninja -C build cxx cxxabi unwind 2>&1 | tee build-runtimes.log | stdbuf -oL awk '
  /FAILED|^error:|ninja: error|Killed|OOM|Linking CXX.*(libc\+\+|libc\+\+abi|libunwind)|build stopped/ { print; fflush() }
'
exit_code=${PIPESTATUS[0]}
echo "$exit_code" > build-runtimes.done
echo "RUNTIMES_EXIT=$exit_code"
