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
| **traffic** | Traffic actions are used to specify how to generate new flows, new streams in each of the new flows, and new packets in each of the new streams. |
| **flow** | Flow actions are used to specify how to generate new streams, and new packets in each of the new streams. |
| **stream** | Stream actions are used to specify how to generate and transfer new packets between two tgen instances. |
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

Example scripts for generating TGen configuration files can be found in the
repository at [tools/scripts/generate_tgen_config.py](../tools/scripts/generate_tgen_config.py)
