#!/bin/sh
set -e

PLNX_PROJ=/home/aurora/p1

petalinux-devtool modify linux-xlnx
cd components/yocto/workspace/sources/linux-xlnx

git remote remove aurora 2>/dev/null || true
git remote add aurora https://github.com/AuroraRennes/linux-xlnx
git fetch aurora ksight:ksight
git checkout ksight

cd "${PLNX_PROJ}"
petalinux-devtool finish linux-xlnx "${PLNX_PROJ}/project-spec/meta-user"

if [ "$1" = "--build" ]; then
    petalinux-build
fi
