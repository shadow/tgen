# Options (attributes)

In [doc/TGen-Overview.md](TGen-Overview.md) we provided an overview how we use a graph to configure TGen. You should read and understand that document first. In this document we specify the full set of options (attributes) that can be set on each action (vertex) in the TGen configuration graph.

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
