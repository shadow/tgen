# TGen Overview

This document explains how to use TGen to generate traffic. It explains how we
use a directed graph to instruct TGen how to generate traffic and outlines the
significance of the vertices, edges, and attributes in the graph. Ultimately,
the `graphml` file containing the graph structure and attributes acts as the
TGen configuration file.

After reading this document, see [doc/TGen-Options.md](TGen-Options.md)
for a full list of options that can be used in TGen.

# Action-dependency graph format

Graph vertices represent **actions**, and graph edges represent
**dependencies**. Each vertex may contain vertex attributes which specify TGen
**options**. Tgen will walk the directed graph path starting at a **start**
vertex, and execute each action along that path. The actions control the
behavior at each stage.

# Actions (vertices)

| Name | Description |
|------|-------------|
| **start** | The **start action is required** for all tgen graph files, and **only one start action is allowed** per tgen instance. |
| **stream** | Stream actions are used to specify how to generate and transfer new packets between two tgen instances. |
| **flow** | Flow actions are used to specify how to generate new streams in the flow, and new packets in each of the new streams. |
| **pause** | Pause actions are used to insert pauses between the execution of transfers. If no time is given, then this action will pause a walk until the vertex has been visited by all incoming edges. More specifically, a pause action without a time is 'activated' when it is first visiting by an incoming edge, and then begins counting visitation by all other incoming edges. Once the 'activated' vertex is visited by all incoming edges, it 'deactivates' and then the walk continues by following each outgoing edge. The pause action acts as a 'barrier'; an example usage would be to start multiple transfers in parallel and wait for them all to finish with a 'pause' before starting the next action. |
| **end** | End actions are used to specify termination conditions for tgen clients; for example, clients can stop after completing a certain number of transfers or transferring a certain amount of data. If any of the configured conditions are met upon arriving at an end action vertex, the tgen node will stop and shutdown. |

# Dependencies (edges)

Edges direct tgen from one action to the next. tgen will perform the above actions by starting at the start vertex and following the edges through the graph. By default, tgen will follow all outgoing edges from a vertex in parallel, thereby creating _multiple concurrent execution paths_ in the graph.

Each edge may contain a 'weight' attribute with a floating point 'double' value, e.g., `weight="1.0"`. While tgen will follow all outgoing edges of a vertex for which no weight is assigned, tgen will choose and follow one and only one edge from the set of weighted outgoing edges of a vertex. A _single execution path_ can be maintained by assigning weights to all outgoing edges of a vertex.

A weighted choice is used to select which weighted outgoing edge of a vertex to follow, based on the sum of weights of all weighted outgoing edges. Therefore, if all weighted outgoing edges have the same weight, the choice will essentially be a uniform random choice.

Be warned that edge weights must be used carefully, especially when combined with the synchronize action. A synchronize action expects that all incoming edges will visit it, which may not be the case if weighted edges were used at some point in a path leading to the synchronize action.

# Examples

An easy way to generate TGen behavior graphs is to use python and the networkx python module.
The scripts are simple, but capable of generating complex behavior profiles.

This script would generate a `tgen.config.graphml` file for a client that:

  + downloads a single 320 KiB file from a server in the server pool;
  + pauses for a time selected uniformly at random between 1 and 60 seconds;
  + repeats.

```python
import networkx as nx

servers="server1:8888,server2:8888"

G = nx.DiGraph()

G.add_node("start", serverport="8888", peers=servers)
G.add_node("transfer", type="get", protocol="tcp", size="320 KiB")
G.add_node("pause", time="1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51,52,53,54,55,56,57,58,59,60")

G.add_edge("start", "transfer")
G.add_edge("transfer", "pause")
G.add_edge("pause", "start")

nx.write_graphml(G, "tgen.web.graphml.xml")
```

And this script would generate a bulk client that repeatedly downloads a single 5 MiB file from a server in the server pool.

```python
import networkx as nx

servers="server1:8888,server2:8888"

G = nx.DiGraph()

G.add_node("start", serverport="8888", peers=servers)
G.add_node("transfer", type="get", protocol="tcp", size="5 MiB")

G.add_edge("start", "transfer")
G.add_edge("transfer", "start")

nx.write_graphml(G, "tgen.bulk.graphml.xml")
```

Here is an example of using multiple transfers and waiting for all of them to complete before moving on to the next action:

```python
import networkx as nx

servers ="server1:8888,server2:8888"


G = nx.DiGraph()

# start is required
G.add_node("start", serverport="8888", peers=servers)

# multiple transfers
G.add_node("transfer1", type="get", protocol="tcp", size="50 KiB")
G.add_node("transfer2", type="get", protocol="tcp", size="1 MiB")
G.add_node("transfer3", type="get", protocol="tcp", size="5 MiB")

# pause for 60 seconds
G.add_node("pause_wait", time="60")
# pause to synchronize transfers
G.add_node("pause_sync")

# after start, go to all transfers in parallel
G.add_edge("start", "transfer1")
G.add_edge("start", "transfer2")
G.add_edge("start", "transfer3")

# now wait for all of those transfers to complete
G.add_edge("transfer1", "pause_sync")
G.add_edge("transfer2", "pause_sync")
G.add_edge("transfer3", "pause_sync")

# now that all transfers are complete, pause for 60 seconds
G.add_edge("pause_sync", "pause_wait")

# repeat the transfers
G.add_edge("pause_wait", "start")

nx.write_graphml(G, "tgen.web.graphml.xml")
```