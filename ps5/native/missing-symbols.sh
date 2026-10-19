#!/bin/bash
# What PS64's archives + objects need that nothing provides, and what resolves only via WebKit's POSIX module.
set -e
B=third_party/ps5-native-app-boilerplate
NM=llvm-nm-18
stubs=$B/.deps/native/ps5-payload-sdk/target/lib
A=($B/ps64/lib/*.a)
objs=$(find $B/build/obj -name '*.o')
sym() { awk 'NF {print $NF}' | LC_ALL=C sort -u; }
w=$(mktemp -d)
$NM -u "${A[@]}" $objs 2>/dev/null | sym > $w/needed
$NM --defined-only "${A[@]}" $objs 2>/dev/null | sym > $w/defined
: > $w/webkit; : > $w/stubs
for s in $stubs/*.so; do
  if [[ ${s##*/} == libScePosixForWebKit.so ]]; then $NM --defined-only -D $s 2>/dev/null | awk 'NF>=2{print $NF}' >> $w/webkit
  else $NM --defined-only -D $s 2>/dev/null | awk 'NF>=2{print $NF}' >> $w/stubs; fi
done
sort -u -o $w/webkit $w/webkit; sort -u -o $w/stubs $w/stubs
# libc.prx shim exports (the clean-room runtime) count as provided too
comm -23 $w/needed $w/defined | comm -23 - $w/stubs > $w/missing
echo "== missing ($(wc -l < $w/missing))"; cat $w/missing | tr '\n' ' '; echo
comm -23 $w/needed $w/defined | comm -12 - $w/webkit > $w/wk
comm -23 $w/wk $w/stubs > $w/wkonly
echo "== only via ScePosixForWebKit ($(wc -l < $w/wkonly))"; cat $w/wkonly | tr '\n' ' '; echo
