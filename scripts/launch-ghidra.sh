#! /bin/bash

rm -rf ghidra-out*

DIR_NAME=$1
uEmu_DIR="/home/dingiso/dev/uemu"
BUILD_DIR="$uEmu_DIR/build"
BUILD=release
INSTALL_DIR="$BUILD_DIR/libs2e-$BUILD/arm-s2e-softmmu"
FIRMWARE="/home/dingiso/dev/uemu/pov/$DIR_NAME/$DIR_NAME.elf"
IMAGENAME=$(basename "$FIRMWARE")

if [ -d "$uEmu_DIR/pov/ghidra-out-$IMAGENAME" ]; then
	PROJECT="-process $IMAGENAME"
else
	mkdir -p $uEmu_DIR/pov/ghidra-out-$IMAGENAME
	touch $uEmu_DIR/pov/ghidra-out-$IMAGENAME/$IMAGENAME.log
	PROJECT="-import $FIRMWARE"
fi

# launch the ghidra loop-detection server in headless mode
$uEmu_DIR/ghidra/support/analyzeHeadless $uEmu_DIR/pov/ghidra-out-$IMAGENAME/ InVulAna $PROJECT -postScript ControlFlowGraph.java -scriptPath $uEmu_DIR/pov/ghidra/CFG/ | tee $uEmu_DIR/pov/ghidra-out-$IMAGENAME/$IMAGENAME.log

