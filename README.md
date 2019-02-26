## TGen

TGen is a C application that generates traffic flows between other
Tgen instances. The characteristics of the traffic (e.g., size, timing,
number of parallel flows, etc.) can be configured by the user.

TGen can generate complex traffic patterns. Users write relatively simple
python scripts to generate `graphml` files that are then used as TGen
configuration files that instruct TGen how to generate traffic.

TGen is used to simulate traffic flows in [Shadow](https://github.com/shadow/shadow),
and to monitor Tor performance in [OnionPerf](https://gitweb.torproject.org/onionperf.git)

## Setup

Dependencies in Fedora/RedHat:

    sudo yum install gcc cmake make glib2 glib2-devel igraph igraph-devel

Dependencies in Ubuntu/Debian:

    sudo apt-get install gcc cmake make libglib2.0 libglib2.0-dev libigraph0v5 libigraph0-dev

Build with a custom install prefix:

    mkdir build && cd build
    cmake .. -DCMAKE_INSTALL_PREFIX=/home/$USER/.local
    make

Optionally install to the prefix:

    make install

## Usage

Run TGen with a single argument (the path to a config file). For example,
first run a server:

    tgen resource/tgen.server.graphml.xml > tgen.server.log

and then run a client that connects to the server:

    tgen resource/tgen.webclient.graphml.xml > tgen.client.log

See the [resource/](resource) directory for example config files.

## More documentation

See [doc/Tools-Setup.md](doc/Tools-Setup.md) for setup instructions for
the TGenTools toolkit that can be used to parse and plot `tgen` log output.

See [doc/TGen-Modeling.md](doc/TGen-Modeling.md) for examples of how to
generate TGen config files with embedded traffic models.

See [doc/TGen-Config.md](doc/TGen-Config.md) for the format of the
configuration file and the possible options that can be used when
generating traffic models.

