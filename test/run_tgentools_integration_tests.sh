#!/bin/bash

# run from base tgen directory

# tgentools must have already been installed in a virtual environment
# in build/toolsenv, or passed in via --tgentools.

TGENTOOLS=
while [[ ${1:-} ]]; do
  case "$1" in
    --tgentools ) TGENTOOLS="$2"; shift 2 ;;
    * ) echo Unrecognized arg $1; exit 1; break ;;
  esac
done

if [ ! "$TGENTOOLS" ]
then
  source build/toolsenv/bin/activate
  TGENTOOLS=$(command -v tgentools)
fi

# Run after build/toolsenv/bin/activate, which this breaks.
set -euo pipefail

fail=0

$TGENTOOLS parse -p build build/tgen.client-web.log
error=$?

if [ $error -eq 0 ]
then
   echo "The 'tgentools parse' command completed successfully."
else
   echo "The 'tgentools parse' command returned a non-zero error code '$error'."
   fail=1
fi

$TGENTOOLS plot --counter-cdfs --prefix build/test -d build/tgen.analysis.json.xz test
error=$?

if [ $error -eq 0 ]
then
   echo "The 'tgentools plot' command completed successfully."
else
   echo "The 'tgentools plot' command returned a non-zero error code '$error'."
   fail=1
fi

if [ $fail -eq 0 ] 
then
    echo "All integration tests passed!" 
else
    echo "At least one integration test failed."
fi
exit ${fail}
