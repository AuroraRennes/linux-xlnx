#!/bin/sh
set -e

PLNX_PROJ=$1

petalinux-devtool modify linux-xlnx
cd components/yocto/workspace/sources/linux-xlnx

# Add aurora as a remote, get the patch the branch was started on and merge changes (squashed)
git remote remove aurora 2>/dev/null || true
git remote add aurora https://github.com/AuroraRennes/linux-xlnx
git fetch origin xlnx_rebase_v6.6_LTS_2024.2
git fetch aurora ksight-2024.2
git merge --squash aurora/ksight-2024.2
git commit -m "ksight squashed patch"


cd "${PLNX_PROJ}"
petalinux-devtool finish linux-xlnx "${PLNX_PROJ}/project-spec/meta-user"

if [ "$2" = "--build" ]; then
    petalinux-build
fi
