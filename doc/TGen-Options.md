# TGen Options (attributes)

In [doc/TGen-Overview.md](TGen-Overview.md) we provided an overview how we use a graph to configure TGen. You should read and understand that document first. In this document we specify the full set of options (attributes) that can be set on each action (vertex) in the TGen configuration graph.

## Option formats

All attributes are currently stored as strings in graphml. When we specify the attributes, we use the following shorthand to describe the value formats:

  + peer: \<ip\>:\<port\>  
    e.g., 127.0.0.1:9050 or 192.168.1.100:8080
  + size: \<integer\> \<suffix\>  
    e.g., "5 suffix" ("5" defaults to "5 bytes") where suffix is case in-sensitive and one of:  
    b, byte, bytes,  
    kb, kilobyte, kilobytes,  
    kib, kibibyte, kibibytes,  
    mb, megabyte, megabytes,  
    mib, mebibyte, mebibytes,  
    gb, gigabyte, gigabytes,  
    gib, gibibyte, gibibytes,  
    tb, terabyte, terabytes,  
    tib, tebibyte, tebibytes
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
| _serverport_ | \<integer\> | 8080 | The local port that will be opened to listen for other tgen connections. Set this value if your tgen instance should act as a server for other tgen client requests. |
| _time_ | \<time\> | 1&nbsp;second | The time that tgen should delay before starting a walk through the action graph. If not given, tgen will start immediately upon process initialization. |
| _heartbeat_ | \<time\> | 1&nbsp;second | The time between which heartbeat status messages are logged at 'message' level. A default of 1 second is used if _heartbeat_ is not set. The heartbeat message is disabled if _heartbeat_ is set to 0. |
| _loglevel_ | \<string\> | info | The level above which tgen log messages will be filtered and not shown or logged. Valid values in increasing order are: 'error', 'critical', 'message', 'info', and 'debug'. The default value if _loglevel_ is not set is 'message'. |
| _*_ | * | * | All options for the **traffic**, **flow**, and **stream** actions specified below can also be set in the **start** action. Any such options that are specified in the **start** action are treated as the global default option for all such actions. You can then override this global default by also setting the option in any individual action. |

## Traffic options

Acceptable attributes for the **traffic** action:

| Name   | Format | Example | Description |
|--------|--------|---------|-------------|
| _flowmodelpath_ | \<filepath\> | ~/flows.graphml | The Markov model to use to generate flows. If unspecified, tgen will use a default Markov model that repeatedly generates flows in both directions at a constant rate and with no inter-flow delay. |
| * | * | * | All options for the **flow** and **stream** actions specified below can also be set in the **traffic** action. Any flow or stream options that are specified in the traffic action will be passed through to all flows and streams generated with the flow and stream Markov models specified for this traffic action (and will therefore override options with the same name that were specified in the start action). |

## Flow options

Acceptable attributes for the **flow** action:

| Name   | Format | Example | Description |
|--------|--------|---------|-------------|
| _streammodelpath_ | \<filepath\> | ~/streams.graphml | The Markov model to use to generate streams in this flow. If unspecified, tgen will use a default Markov model that repeatedly generates streams in both directions at a constant rate and with no inter-stream delay. |
| * | * | * | All options for the **stream** action specified below can also be set in the **flow** action. Any stream options that are specified in the flow action will be passed through to all streams generated with the stream Markov model specified for this flow (and will therefore override options with the same name that were specified in the start action). |

## Stream options

Acceptable attributes for the **stream** action:

| Name   | Format | Example | Description |
|--------|--------|---------|-------------|
| _packetmodelpath_ | \<filepath\> | ~/packets.graphml | The Markov model to use to generate packets in this stream. If unspecified, tgen will use an internal default Markov model with no end state that causes tgen to continously generates packets in both directions with no inter-packet delay. |
| _packetmodelmode_ | \<string\> | 'path' or 'graphml' | Sets the mode that tgen uses to send the packet Markov model to the server. If unset or set to 'graphml', the tgen client will load the model locally and send a string representation of the model formatted as graphml during the stream handshake. If set to 'path', the client will send the path given in the _packetmodelpath_ attribute to the server, and the server will attempt to load the model at that same path (the server must already have a copy of the model). |
| _markovmodelseed_ | \<integer\> | 12345 | The seed that will be used to initialize a pseudorandom number generator (prng) that will generate seeds for all Markov models created for this action. If unspecified, tgen initializes the prng using a seed generated by a global prng that was randomly seeded. |
| _peers_ | \<peer\>,... | 10.0.0.1,... | **Required:** a comma-separated list of peers to use for this **stream**. The _peers_ attribute is **required** unless a _peers_ attribute is specified in the **start** action. A peer will be selected at random from this list, or at random from the **start** action list if this attribute is not specified. |
| _socksproxy_ | \<peer\>,... | 127.0.0.1:9050 | A comma-separated list of peers to use as proxy servers through which all connections to other tgen peers will be made. If not given, tgen will connect to the peer directly unless this option is set in the **start** action. |
| _socksusername_ | \<string\> | myuser | The SOCKS username that we should send to the SOCKS proxy during the SOCKS handshake for this stream. This option is ignored unless _socksproxy_ is also set in either this **stream** action or in the **start** action. |
| _sockspassword_ | \<string\> | mypass | The SOCKS password that we should send to the SOCKS proxy during the SOCKS handshake for this stream. This option is ignored unless _socksproxy_ is also set in either this **stream** action or in the **start** action. |
| _socksauthseed_ | \<integer\> | 12345 | If set, the seed that will be used to initialize a pseudorandom number generator (prng) that will generate random _socksusername_ and _sockspassword_ strings that we send to the SOCKS proxy during the SOCKS handshake. If set on a **stream** action, random strings will be generated for each stream; otherwise, random strings will be generated for each **flow**, and all streams generated by a flow will use the same strings as the flow that generated them. This option is ignored unless _socksproxy_ is also set, and overrides any _socksusername_ and _sockspassword_ options that were set. |
| _sendsize_ | \<size\> | 5&nbsp;KiB | The amount of payload data to send from the client to the server after completing the initial handshake. If not set, then the client will continue sending data until it reaches the end state in the Markov model. If set to a positive value, the Markov model will be reset and repeated as necessary until the _sendsize_ is reached. If set to 0, the client will send no payload data following the handshake. To send an infinite stream, either configure a Markov model with no 'F' emissions or don't configure a Markov model (in which case an internal nonstop model will be used). |
| _recvsize_ | \<size\> | 10&nbsp;MiB | This is analagous to the _sendsize_ option, but for payload data received by the client from the server (i.e., payload data sent from the server to the client). |
| _timeout_ | \<time\> | 2&nbsp;minutes | The amount of time since the stream started after which we give up on it. If specified, this stream overrides any _timeout_ attribute that may have been set on the **start** action. If this is set to 0, then an absolute timeout is disabled. If this is not set and is also not set in **start** action, then an absolute timeout is disabled. |
| _stallout_ | \<time\> | 30&nbsp;seconds | The amount of time since bytes were last sent or received for this stream after which we consider this a stalled stream and give up on it. If specified, this stream overrides any _stallout_ attribute that may have been set on the **start** element. If this is set to 0, then a stallout is disabled. If this is not set and is also not set in **start** action, then an internally defined stallout is used instead (currently 30 seconds). |

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
