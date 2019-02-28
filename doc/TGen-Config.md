This document explains how to configure tgen with a `tgen.config.graphml.xml` file.

# Action-dependency graph format

Graph vertices represent **actions**, and graph edges represent **dependencies**. Each vertex may contain vertex attributes which specify action **parameters**. Tgen will walk the directed graph path starting at a start vertex, and execute each action along that path. The actions control the behavior at each stage.

# Actions (vertices)

| Name | Description |
|------|-------------|
| **start** | The **start action is required** for all tgen graph files, and **only one start action is allowed** per tgen instance. |
| **stream** | Stream actions are used to specify how to generate and transfer new packets between two tgen instances. |
| **flow** | Flow actions are used to specify how to generate new streams in the flow, and new packets in each of the new streams. |
| **pause** | Pause actions are used to insert pauses between the execution of transfers. If no time is given, then this action will pause a walk until the vertex has been visited by all incoming edges. More specifically, a pause action without a time is 'activated' when it is first visiting by an incoming edge, and then begins counting visitation by all other incoming edges. Once the 'activated' vertex is visited by all incoming edges, it 'deactivates' and then the walk continues by following each outgoing edge. The pause action acts as a 'barrier'; an example usage would be to start multiple transfers in parallel and wait for them all to finish with a 'pause' before starting the next action. |
| **end** | End actions are used to specify termination conditions for tgen clients; for example, clients can stop after completing a certain number of transfers or transferring a certain amount of data. If any of the configured conditions are met upon arriving at an end action vertex, the tgen node will stop and shutdown. |

# Attributes (options)

All attributes are currently stored as strings in graphml. When we specify the attributes, we use the following shorthand to describe the value formats:

  + peer: \<ip\>:\<port\>  
  e.g., 127.0.0.1:9050 or 192.168.1.100:8080
  + size: \<integer\> \<suffix\>  
  e.g., "5 suffix" ("5" defaults to "5 bytes") where suffix is case in-sensitive and one of: kb, mb, gb, tb, kib, mib, gib, tib
  + time: \<integer\> \<suffix\>  
  e.g., "60 suffix" ("60" defaults to "60 seconds") where suffix is case in-sensitive and one of:  
nanosecond, nanoseconds, nsec, nsecs, ns,  
microsecond, microseconds, usec, usecs, us,  
millisecond, milliseconds, msec, msecs, ms,  
second, seconds, sec, secs, s,  
minute, minutes, min, mins, m,  
hour, hours, hr, hrs, h

## Start options

Acceptable attributes for the **start** action:

| Name   | Format | Example | Description |
|--------|--------|---------|-------------|
| _serverport_ | \<integer\> | 8080 | **Required:** the local port that will be opened to listen for other tgen connections. This is **required** if your tgen instance should act as a server. |
| _peers_ | \<peer\>,... | 10.0.0.1,... | **Required:** a comma-separated list of peers to use for transfers that do not explicitly specify a peer. The _peers_ attribute is **required** unless all transfers specify thier own _peers_ attribute. |
| _time_ | \<time\> | 1&nbsp;second | The time that tgen should delay before starting a walk through the action graph. If not given, tgen will start immediately upon process initialization. |
| _socksproxy_ | \<peer\> | 127.0.0.1:9050 | A peer to use as a proxy server through which all connections to other tgen peers will be made. If not given, tgen will connect to the peer directly. |
| _timeout_ | \<time\> | 60&nbsp;seconds | The default time since the transfer started after which we give up on stalled transfers, used for all incoming server-side transfers and all client transfers that do not explicitly specify a _timeout_ attribute. If this is not set or set to 0 and not overridden by the transfer, then an internally defined timeout is used instead (currently 60 seconds). |
| _stallout_ | \<time\> | 15&nbsp;seconds | The default time since bytes were last sent/received for this transfer after which we give up on stalled transfers, used for all incoming server-side transfers and all client transfers that do not explicitly specify a _stallout_ attribute. If this is not set or set to 0 and not overridden by the transfer, then an internally defined stallout is used instead (currently 15 seconds). |
| _heartbeat_ | \<time\> | 1&nbsp;second | The time between which heartbeat status messages are logged at 'message' level. The default of 1 second is used if _heartbeat_ is 0 or is not set. |
| _loglevel_ | \<string\> | info | The level above which tgen log messages will be filtered and not shown or logged. Valid values in increasing order are: 'error', 'critical', 'message', 'info', and 'debug'. The default value if _loglevel_ is not set is 'message'. |

## Stream options

Acceptable attributes for the **stream** action:

| Name   | Format | Example | Description |
|--------|--------|---------|-------------|
| _peers_ | \<peer\>,... | 10.0.0.1,... | **Required:** a comma-separated list of peers to use for this transfer. The _peers_ attribute is **required** unless a _peers_ attribute is specified in the start action. A peer will be selected at random from this list, or at random from the start action list if this attribute is not specified for a transfer. |
| _packetmodel_ | \<filepath\> | ~/packets.graphml | The markov model to use to generate packets. If unspecified, tgen will use a default markov model that repeatedly generates packets in both directions at a constant rate and with no inter-packet delay. I.e., both the client and server will send as many packets as possible as fast as possible. |
| _sendsize_ | \<size\> | 5&nbsp;KiB | The amount of data to send from the client to the server. If not set or set to 0, then the client will continue sending data until it reaches the end state in the markov model. If set to a positive value, the markov model will be reset and repeated as necessary until the _sendsize_ is reached. |
| _recvsize_ | \<size\> | 10&nbsp;MiB | The amount of data to send from the server to the client. If not set or set to 0, then the server will continue sending data until it reaches the end state in the markov model. If set to a positive value, the markov model will be reset and repeated as necessary until the _recvsize_ is reached. |
| _timeout_ | \<time\> | 60&nbsp;seconds | the time since the transfer started after which we consider this a stalled transfer and give up on it. If specified, this overrides the default _timeout_ attribute of the **start** element for this specific transfer. If this is set to 0, then an internally defined timeout is used instead (currently 60 seconds). |
| _stallout_ | \<time\> | 15&nbsp;seconds | the time since bytes were last sent/received for this transfer after which we consider this a stalled transfer and give up on it. If specified, this overrides the default _stallout_ attribute of the **start** element for this specific transfer. If this is set to 0, then an internally defined stallout is used instead (currently 15 seconds). |

## Flow options

Acceptable attributes for the **flow** action:

| Name   | Format | Example | Description |
|--------|--------|---------|-------------|
| _peers_ | \<peer\>,... | 10.0.0.1,... | As specified in the stream action table. |
| _streammodel_ | \<filepath\> | ~/streams.graphml | The markov model to use to generate streams. If unspecified, tgen will use a default markov model that repeatedly generates streams in both directions at a constant rate and with no inter-stream delay. |
| _packetmodel_ | \<filepath\> | ~/packets.graphml | As specified in the stream action table. |
| _sendsize_ | \<size\> | 5&nbsp;KiB | As specified in the stream action table. |
| _recvsize_ | \<size\> | 10&nbsp;MiB | As specified in the stream action table. |
| _timeout_ | \<time\> | 60&nbsp;seconds | As specified in the stream action table. |
| _stallout_ | \<time\> | 15&nbsp;seconds | As specified in the stream action table. |

## Pause options

Acceptable attributes for the **pause** action:

| Name   | Format | Example | Description |
|--------|--------|---------|-------------|
| _time_ | \<time\> | 5&nbsp;seconds | the time that the tgen node should pause before resuming the walk through the action graph.  |

## End options

Acceptable attributes for the **end** action:

| Name   | Format | Example | Description |
|--------|--------|---------|-------------|
| _time_ | \<time\> | 10&nbsp;minutes | Stop if this amount of time has passed since tgen started. |
| _stop_ | \<integer\> | 10 | Stop if tgen has completed (successfully or not) this number of streams. |
| _sendsize_ | \<size\> | 1&nbsp;GiB | Stop if this amount of data was sent by the client. |
| _recvsize_ | \<size\> | 1&nbsp;GiB | Stop if this amount of data was received by the client. |

# Dependencies (edges)

Edges direct tgen from one action to the next. tgen will perform the above actions by starting at the start vertex and following the edges through the graph. By default, tgen will follow all outgoing edges from a vertex in parallel, thereby creating _multiple concurrent execution paths_ in the graph.

Each edge may contain a 'weight' attribute with a floating point 'double' value, e.g., `weight="1.0"`. While tgen will follow all outgoing edges of a vertex for which no weight is assigned, tgen will choose and follow one and only one edge from the set of weighted outgoing edges of a vertex. A _single execution path_ can be maintained by assigning weights to all outgoing edges of a vertex.

A weighted choice is used to select which weighted outgoing edge of a vertex to follow, based on the sum of weights of all weighted outgoing edges. Therefore, if all weighted outgoing edges have the same weight, the choice will essentially be a uniform random choice.

Be warned that edge weights must be used carefully, especially when combined with the synchronize action. A synchronize action expects that all incoming edges will visit it, which may not be the case if weighted edges were used at some point in a path leading to the synchronize action.