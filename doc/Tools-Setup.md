# TGenTools

TGenTools is a toolkit to parse and plot `tgen` log files. TGenTools is
not required to run `tgen`, but may be helpful to understand its output.

## Install system dependencies

Dependencies in Fedora/RedHat:

    sudo yum install python python-devel python-pip python-virtualenv libxml2 libxml2-devel libxslt libxslt-devel libpng libpng-devel freetype freetype-devel blas blas-devel lapack lapack-devel

Dependencies in Ubuntu/Debian:

    sudo apt-get install python python-dev python-pip python-virtualenv libxml2 libxml2-dev libxslt1.1 libxslt1-dev libpng12-0 libpng12-dev libfreetype6 libfreetype6-dev libblas-dev liblapack-dev

## Install TGenTools Python modules

We show how to install python modules using `pip` (although you can also
use your OS package manager). We recommend using virtual environments to
keep all of the dependencies self-contained and to avoid conflicts with
your other python projects.

    virtualenv --no-site-packages tgenenv
    source tgenenv/bin/activate
    pip install -r path/to/tgen/tools/requirements.txt
    pip install -I path/to/tgen/tools

## Run TGenTools

TGenTools has several modes of operation and a help menu for each. For a
description of each mode, use the following (make sure you have activated
the tgen virtual environment with `source tgenenv/bin/activate` first):

```
tgentools -h
```

  + **parse**: Analyze TGen output
  + **plot**: Visualize TGen analysis results
  + **edit**: Edit TGen configuration files in place

## Example parsing and plotting TGen output

Assuming you have already run `tgen` and saved the output to a log file
called `tgen.client.log`, you can then parse the log file like this:

    tgentools parse tgen.client.log

This produces the `tgen.analysis.json.xz` file, the format of which is
outlined in [doc/Tools-JSON-Format.md](Tools-JSON-Format.md).
The analysis file can be plotted:

    tgentools plot --data tgen.analysis.json.xz "tgen-test"

This will save new PDFs containing several graphs in the current directory.
Depending on the data that was analyzed, the graphs may include:

- Time to download first byte of transfer, across all transfers
- Time to download last byte of transfer, across all transfers
- Median time to download last byte of transfer, per client
- Mean time to download last byte of transfer, per client
- Max time to download last byte of transfer, per client
- Number of transfer successes, per client
- Number of transfer errors, per client
- Bytes transferred before error, across all transfers with error
- Median bytes transferred before error, per client
- Mean bytes transferred before error, per client
