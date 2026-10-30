#!/bin/sh
# FisixDreamcast KOS environ, workspace paths.
# Source this before make:  . ./environ.sh
export KOS_ARCH="dreamcast"
export KOS_SUBARCH="pristine"
export KOS_TOOLCHAIN="gcc"
export KOS_BASE="/home/hatch/workspace/toolchain-dreamcast/kos"
export KOS_CC_BASE="/home/hatch/workspace/toolchain-dreamcast/sh-elf"
export KOS_CC_PREFIX="sh-elf"
export KOS_PORTS="/home/hatch/workspace/toolchain-dreamcast/kos-ports"
export PATH="$KOS_CC_BASE/bin:$KOS_BASE/utils/bin2o:$KOS_BASE/utils:$PATH"
# KOS shared setup: adds utils/build_wrappers (kos-cc etc.) to PATH,
# sets KOS_CC, KOS_VERSION, KOS_INC_PATHS from the sample environ flow.
. "${KOS_BASE}/env/environ_base.sh"
