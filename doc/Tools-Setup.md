# TGenTools

TGenTools is a toolkit to parse and plot `tgen` log files. TGenTools is
not required to run `tgen`, but may be helpful to understand its output.

## Install system dependencies

Dependencies in Fedora/RedHat:

    sudo yum install python python-devel libxml2 libxml2-devel libxslt libxslt-devel libpng libpng-devel freetype freetype-devel

Dependencies in Ubuntu/Debian:

    sudo apt-get install python python-dev libxml2 libxml2-dev libxslt1.1 libxslt1-dev libpng12-0 libpng12-dev libfreetype6 libfreetype6-dev

## Install required Python modules

We show how to install python modules using `pip` (although you can also
use your OS package manager). We recommend using virtual environments to
keep all of the dependencies self-contained and to avoid conflicts with
your other python projects.

    pip install virtualenv
    virtualenv --no-site-packages venv
    source venv/bin/activate
    pip install -r requirements.txt
    deactivate

## Build and Install TGenTools

    source venv/bin/activate
    cd tgen/tools
    pip install -I .

## Run TGenTools

TGenTools has several modes of operation and a help menu for each. For a
description of each mode, use:

```
tgentools -h
```

  + **parse**: Analyze TGen output
  + **plot**: Visualize TGen analysis results

## Example parsing and plotting TGen output

Assuming you have already run `tgen` and saved the output to a log file
called `tgen.client.log`, you can then parse the log file like this:

    tgentools parse tgen.client.log

This produces the `tgen.analysis.json.xz` file, the format of which is
outlined in `doc/Tools-JSON-Format.md`. The analysis file can be plotted:

    tgentools plot --data tgen.analysis.json.xz "tgen-test"

This will save new PDFs containing several graphs in the current directory.
Depending on the data that was analyzed, the graphs may include:

- Number of transfer AUTH errors, each client
- Number of transfer PROXY errors, each client
- Number of transfer AUTH errors, all clients over time
- Number of transfer PROXY errors, all clients over time
- Bytes transferred before AUTH error, all downloads
- Bytes transferred before PROXY error, all downloads
- Median bytes transferred before AUTH error, each client
- Median bytes transferred before PROXY error, each client
- Mean bytes transferred before AUTH error, each client
- Mean bytes transferred before PROXY error, each client
