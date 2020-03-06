#!/bin/bash

# run from base tgen directory
# tgentools must have already been installed in a virtual environment
# in build/toolsenv

source build/toolsenv/bin/activate

# must set these options *after* sourcing the above environment
set -euo pipefail

fail=0

tgentools parse -p build build/tgen.client-web.log
error=$?

if [ $error -eq 0 ]
then
   echo "The 'tgentools parse' command completed successfully."
else
   echo "The 'tgentools parse' command returned a non-zero error code '$error'."
   fail=1
fi

tgentools plot --counter-cdfs --prefix build/test -d build/tgen.analysis.json.xz test
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
