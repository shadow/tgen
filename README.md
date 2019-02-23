## TGen

Tgen is a C application that models traffic behaviors using an
action-dependency graph represented using the standard `graphml.xml`
format. Each tgen node takes a graphml-formatted file as input, and
then begins transferring data to/from other nodes by following a path
through the action graph.

TGen is used to simulate traffic flows in [Shadow](https://github.com/shadow/shadow)

TGen is used to monitor Tor performance in [OnionPerf](https://gitweb.torproject.org/onionperf.git)

## Setup

- Dependencies in Fedora/RedHat:

        sudo yum install gcc cmake make glib2 glib2-devel igraph igraph-devel

- Dependencies in Ubuntu/Debian:

        sudo apt-get install gcc cmake make libglib2.0 libglib2.0-dev libigraph0v5 libigraph0-dev

- Build with a custom install prefix:

        mkdir build && cd build
        cmake .. -DCMAKE_INSTALL_PREFIX=/home/$USER/.local
        make

- Optionally install to the install prefix:

        make install # optional, installs to path set above

## Usage

Run TGen with a single argument:

    tgen path/to/tgen.config.graphml.xml

See the `resource/` directory for example config files, and see the `doc/`
directory to learn more about the graph file and how you can generate your own
in order to model your intended behavior.
