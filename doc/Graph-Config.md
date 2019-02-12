This document explains how to configure tgen with a `tgen.config.graphml.xml` file.

### Action-dependency graph format

Graph vertices represent **actions**, and graph edges represent **dependencies**. Each vertex may contain vertex attributes which specify action **parameters**. Tgen will walk the directed graph path starting at a start vertex, and execute each action along that path. The actions control the behavior at each stage.

#### Actions (vertices)

The following are valid **actions** and **parameters** (all parameters are currently stored as strings in graphml):

**start:** The **start action is required** for all tgen graph files, and **only one start action is allowed** per tgen instance. Acceptable attributes are:

  + _serverport_ (required):  
the local port that will be opened to listen for other tgen connections
  + _time_ (optional):  
the time (see format below) that the tgen node should delay before starting a walk through the action graph
  + _socksproxy_ (optional):  
a peer (`ip:port`, e.g., `127.0.0.1:9050`) to use as a proxy server through which all connections to other tgen peers will be made
  + _timeout_ (optional):  
the default time (see format below) since the transfer started after which we give up on stalled transfers, used for all incoming server-side transfers and all client transfers that do not explicitly specify a _timeout_ attribute. If this is not set or set to 0 and not overridden by the transfer, then an internally defined timeout is used instead (currently 60 seconds).
  + _stallout_ (optional):  
the default time (see format below) since bytes were last sent/received for this transfer after which we give up on stalled transfers, used for all incoming server-side transfers and all client transfers that do not explicitly specify a _stallout_ attribute. If this is not set or set to 0 and not overridden by the transfer, then an internally defined stallout is used instead (currently 15 seconds).
  + _heartbeat_ (optional):  
the time period (see format below) between which heartbeat status messages are logged at 'message' level. The default of 1 second is used if _heartbeat_ is 0 or is not set.
  + _loglevel_ (optional):  
the level above which tgen log messages will be filtered and not shown or logged. Valid values in increasing order are: 'error', 'critical', 'message', 'info', and 'debug'. The default value if _loglevel_ is not set is 'message'.
  + _peers_ (special):  
a list of peers (`ip1:port1,ip2:port21`, e.g., `192.168.1.100:8888,192.168.1.101:8888`) to use for transfers that do not explicitly specify a peer. The _peers_ attribute is optional, only if all transfers specify a _peers_ attribute.

**transfer:** Transfer actions are optional. Acceptable attributes are:

  + _type_ (required):  
type of transfer: "get" to download or "put" to upload
  + _protocol_ (required):  
protocol to use for this transfer (only "tcp" is supported)
  + _size_ (required):  
amount of data to transfer (see format below)
  + _timeout_ (optional):  
the time (see format below) since the transfer started after which we consider this a stalled transfer and give up on it. If specified, this overrides the default _timeout_ attribute of the **start** element for this specific transfer. If this is set to 0, then an internally defined timeout is used instead (currently 60 seconds).
  + _stallout_ (optional):  
the time (see format below) since bytes were last sent/received for this transfer after which we consider this a stalled transfer and give up on it. If specified, this overrides the default _stallout_ attribute of the **start** element for this specific transfer. If this is set to 0, then an internally defined stallout is used instead (currently 15 seconds).
  + _peers_ (special):  
a list of peers (`ip1:port1,ip2:port21`, e.g., `192.168.1.100:8888,192.168.1.101:8888`) to use for this transfer. The _peers_ attribute is optional, only if a _peers_ attribute is specified in the start action. A peer will be selected at random from this list, or at random from the start action list if this attribute is not specified for a transfer.

**pause:** Pause actions are optional. Acceptable attributes are:

  + _time_ (optional):  
the time (see format below) that the tgen node should pause before resuming the walk through the action graph.  
  
If no time is given, then this action will pause a walk until the vertex has been visited by all incoming edges. More specifically, a pause action without a time is 'activated' when it is first visiting by an incoming edge, and then begins counting visitation by all other incoming edges. Once the 'activated' vertex is visited by all incoming edges, it 'deactivates' and then the walk continues by following each outgoing edge.  
  
The pause action acts as a 'barrier'; an example usage would be to start multiple transfers in parallel and wait for them all to finish with a 'pause' before starting the next action.

**end:** End actions are optional. The parameters represent termination conditions: if any of the conditions are met upon arriving at an end action vertex, the tgen node will stop and shutdown. Acceptable attributes are:

  + _time_ (optional):  
the time (see format below) since the node started
  + _count_ (optional):  
the number of transfer completed by this node
  + _size_ (optional):  
the total amount of data (see format below) transferred (read+write) by this node

Attribute value formats:

  + size: e.g., "5", or "5 suffix" where suffix is case in-sensitive and one of: kb, mb, gb, tb, kib, mib, gib, tib
  + time: e.g., "60" (defaults to seconds), or "60 suffix" where suffix is case in-sensitive and one of:  
nanosecond, nanoseconds, nsec, nsecs, ns,  
microsecond, microseconds, usec, usecs, us,  
millisecond, milliseconds, msec, msecs, ms,  
second, seconds, sec, secs, s,  
minute, minutes, min, mins, m,  
hour, hours, hr, hrs, h

#### Dependencies (edges)

Edges direct tgen from one action to the next. tgen will perform the above actions by starting at the start vertex and following the edges through the graph. By default, tgen will follow all outgoing edges from a vertex in parallel, thereby creating _multiple concurrent execution paths_ in the graph.

Each edge may contain a 'weight' attribute with a floating point 'double' value, e.g., `weight="1.0"`. While tgen will follow all outgoing edges of a vertex for which no weight is assigned, tgen will choose and follow one and only one edge from the set of weighted outgoing edges of a vertex. A _single execution path_ can be maintained by assigning weights to all outgoing edges of a vertex.

A weighted choice is used to select which weighted outgoing edge of a vertex to follow, based on the sum of weights of all weighted outgoing edges. Therefore, if all weighted outgoing edges have the same weight, the choice will essentially be a uniform random choice.

Be warned that edge weights must be used carefully, especially when combined with the synchronize action. A synchronize action expects that all incoming edges will visit it, which may not be the case if weighted edges were used at some point in a path leading to the synchronize action.