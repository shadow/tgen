This document explains how to use python to write scripts that generate `tgen.config.graphml.xml` files that instruct tgen to generate traffic according to your desired application behaviors.

### Customizing generator behaviors

Given the above actions and parameters, simple python scripts can be used to generate behavior models using the networkx python module.

This script would generate a tgen configuration file for a client that:

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

The scripts are simple, but capable of generating complex behavior profiles.

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