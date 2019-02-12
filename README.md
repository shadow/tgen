## TGen

Tgen is a C application that models traffic behaviors using an action-dependency
graph represented using the standard `graphml.xml` format. Each tgen node
takes a graphml-formatted file as a parameter, and then begins transferring data
to/from other nodes by following a path through the action graph.

## Setup

```
mkdir build && cd build
cmake .. -DCMAKE_INSTALL_PREFIX=/home/$USER/.local
make
make install # optional, installs to path set above
```

## Usage

tgen path/to/tgen.config.xml

See the `resource/` directory for example config files, and see the `doc/`
directory to learn more about the graph file and how you can generate your own
in order to model your intended behavior.

