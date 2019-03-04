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

    sudo yum install cmake glib2 glib2-devel igraph igraph-devel

Dependencies in Ubuntu/Debian:

    sudo apt-get install cmake libglib2.0 libglib2.0-dev libigraph0v5 libigraph0-dev

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

See [doc/TGen-Overview.md](doc/TGen-Overview.md) for an overview of how to use
a graph to instruct TGen how it should generate traffic, and then see
[doc/TGen-Options.md](doc/TGen-Options.md) for a description of all options
supported by TGen.

See [doc/TGen-Markov-Models.md](doc/TGen-Markov-Models.md) for a description
of how to create and use markov models to instruct TGen how to generate
streams in a traffic flow and packets in a stream.
