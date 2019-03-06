# TGen Options (attributes)

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
| _time_ | \<time\> | 1&nbsp;second | The time that tgen should delay before starting a walk through the action graph. If not given, tgen will start immediately upon process initialization. |
| _heartbeat_ | \<time\> | 1&nbsp;second | The time between which heartbeat status messages are logged at 'message' level. The default of 1 second is used if _heartbeat_ is 0 or is not set. |
| _loglevel_ | \<string\> | info | The level above which tgen log messages will be filtered and not shown or logged. Valid values in increasing order are: 'error', 'critical', 'message', 'info', and 'debug'. The default value if _loglevel_ is not set is 'message'. |

**NOTE:** all options for the **stream** action specified below can also be set in the **start** action. Any stream options that are specified in the **start** action are treated as the global default option for all streams. You can then override this global default by also setting that option in any individual **stream** action. (You could also just specify stream options in every **stream** action and never set a global value in the **start** action.)

## Stream options

Acceptable attributes for the **stream** action:

| Name   | Format | Example | Description |
|--------|--------|---------|-------------|
| _packetmodelpath_ | \<filepath\> | ~/packets.graphml | The Markov model to use to generate packets. If unspecified, tgen will use a default Markov model that repeatedly generates packets in both directions at a constant rate and with no inter-packet delay. I.e., both the client and server will send as many packets as possible as fast as possible. |
| _packetmodelseed_ | \<integer\> | 12345 | The seed that will be used to initialize the pseudorandom generator in the packet Markov model. If unspecified, tgen generates a seed using a global pseudorandom generator that was randomly seeded. |
| _peers_ | \<peer\>,... | 10.0.0.1,... | **Required:** a comma-separated list of peers to use for this **stream**. The _peers_ attribute is **required** unless a _peers_ attribute is specified in the **start** action. A peer will be selected at random from this list, or at random from the **start** action list if this attribute is not specified for a **stream**. |
| _socksproxy_ | \<peer\> | 127.0.0.1:9050 | A peer to use as a proxy server through which all connections to other tgen peers will be made. If not given, tgen will connect to the peer directly unless this option is set in the **start** action. |
| _socksusername_ | \<string\> | myuser | The SOCKS username that we should send to the SOCKS proxy during the SOCKS handshake for for this stream. This option is ignored unless _socksproxy_ is also set in either this **stream** action or in the **start** action. |
| _sockspassword_ | \<string\> | mypass | The SOCKS password that we should send to the SOCKS proxy during the SOCKS handshake for for this stream. This option is ignored unless _socksproxy_ is also set in either this **stream** action or in the **start** action. |
| _sendsize_ | \<size\> | 5&nbsp;KiB | The amount of data to send from the client to the server. If not set or set to 0, then the client will continue sending data until it reaches the end state in the Markov model. If set to a positive value, the Markov model will be reset and repeated as necessary until the _sendsize_ is reached. |
| _recvsize_ | \<size\> | 10&nbsp;MiB | The amount of data to send from the server to the client. If not set or set to 0, then the server will continue sending data until it reaches the end state in the Markov model. If set to a positive value, the Markov model will be reset and repeated as necessary until the _recvsize_ is reached. |
| _timeout_ | \<time\> | 60&nbsp;seconds | The amount of time since the stream started after which we give up on it. If specified, this stream overrides any _timeout_ attribute that may have been set on the **start** action. If this is set to 0, then an absolute timeout is disabled. If this is not set and is also not set in **start** action, then an absolute timeout is disabled. |
| _stallout_ | \<time\> | 15&nbsp;seconds | The amount of time since bytes were last sent or received for this stream after which we consider this a stalled stream and give up on it. If specified, this stream overrides any _stallout_ attribute that may have been set on the **start** element. If this is set to 0, then a stallout is disabled. If this is not set and is also not set in **start** action, then an internally defined stallout is used instead (currently 60 seconds). |

## Flow options

Acceptable attributes for the **flow** action:

| Name   | Format | Example | Description |
|--------|--------|---------|-------------|
| _streammodelpath_ | \<filepath\> | ~/streams.graphml | The Markov model to use to generate streams. If unspecified, tgen will use a default Markov model that repeatedly generates streams in both directions at a constant rate and with no inter-stream delay. |
| _streammodelseed_ | \<integer\> | 12345 | The seed that will be used to initialize the pseudorandom generator in the stream Markov model. If unspecified, tgen generates a seed using a global pseudorandom generator that was randomly seeded. |

**NOTE**: all options for the **stream** action specified above can also be set in the **flow** action. Any stream options that are specified in the **flow** action will be passed through to all streams generated with the stream Markov model specified for this flow (and will therefore override options with the same name that were specified in the **start** action).

## Pause options

Acceptable attributes for the **pause** action:

| Name   | Format | Example | Description |
|--------|--------|---------|-------------|
| _time_ | \<time\>,... | 5&nbsp;seconds,... | The time or comma-separated list of times that the tgen node should pause before resuming the walk through the action graph. If a list is given, a time from the list will be chosen uniformly at random every time the pause action is encountered while walking the graph.  |

## End options

Acceptable attributes for the **end** action:

| Name   | Format | Example | Description |
|--------|--------|---------|-------------|
| _time_ | \<time\> | 10&nbsp;minutes | Stop if this amount of time has passed since tgen started. |
| _count_ | \<integer\> | 10 | Stop if tgen has completed (successfully or not) this number of streams. |
| _sendsize_ | \<size\> | 1&nbsp;GiB | Stop if this amount of data was sent by the client. |
| _recvsize_ | \<size\> | 1&nbsp;GiB | Stop if this amount of data was received by the client. |
