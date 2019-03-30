#!/usr/bin/python

import sys
import networkx as nx

# This script generates several example tgen config files.
# Generally, the client drives the download process and
# connects to a server.

def main():
    generate_server()
    generate_client_web()
    generate_client_singlefile()
    generate_client_delayed()
    generate_client_nonstop_flow()
    generate_client_nonstop_stream()

def generate_server():
    G = nx.DiGraph()
    G.add_node("start", serverport="8888", heartbeat="1 second", loglevel="message")
    nx.write_graphml(G, "server.tgenrc.graphml")

def generate_client_web():
    G = nx.DiGraph()

    G.add_node("start", time="1 second", heartbeat="1 second", loglevel="message", peers="localhost:8888")

    G.add_node("streamA", sendsize="1 kib", recvsize="1 mib")
    G.add_node("streamB1", sendsize="10 kib", recvsize="1 mib")
    G.add_node("streamB2", sendsize="100 kib", recvsize="10 mib")
    G.add_node("streamB3", sendsize="1 mib", recvsize="100 mib")

    G.add_node("pause_sync")
    G.add_node("pause", time="1,2,3,4,5,6,7,8,9,10")
    G.add_node("end", time="1 minute", count="30", recvsize="1 GiB", sendsize="1 GiB")

    # a small first round request
    G.add_edge("start", "streamA")
    # second round requests in parallel
    G.add_edge("streamA", "streamB1")
    G.add_edge("streamA", "streamB2")
    G.add_edge("streamA", "streamB3")
    # wait for both second round streams to finish
    G.add_edge("streamB1", "pause_sync")
    G.add_edge("streamB2", "pause_sync")
    G.add_edge("streamB3", "pause_sync")
    # check if we should stop
    G.add_edge("pause_sync", "end")
    # pause for a short time and then start again
    G.add_edge("end", "pause")
    G.add_edge("pause", "start")

    nx.write_graphml(G, "client-web.tgenrc.graphml")

def generate_client_singlefile():
    G = nx.DiGraph()

    G.add_node("start", time="1 second", heartbeat="1 second", loglevel="message", peers="localhost:8888")
    G.add_node("stream", sendsize="1 kib", recvsize="10 mib")
    G.add_node("end", count="10", time="1 minute")
    G.add_node("pause", time="1,2,3,4,5")

    G.add_edge("start", "stream")
    G.add_edge("stream", "end")
    G.add_edge("end", "pause")
    G.add_edge("pause", "start")

    nx.write_graphml(G, "client-singlefile.tgenrc.graphml")

def generate_client_delayed():
    G = nx.DiGraph()

    G.add_node("start", heartbeat="1 second", loglevel="message", peers="localhost:8888")
    G.add_node("flow", markovmodelseed="12345", streammodelpath="delayed.streammodel.graphml", packetmodelpath="delayed.packetmodel.graphml")
    G.add_node("end", count="4")

    G.add_edge("start", "flow")
    G.add_edge("flow", "end")
    G.add_edge("end", "start")

    nx.write_graphml(G, "client-delayed.tgenrc.graphml")

def generate_client_nonstop_flow():
    G = nx.DiGraph()

    G.add_node("start", heartbeat="1 second", loglevel="message", peers="localhost:8888")
    G.add_node("flow", markovmodelseed="12345", streammodelpath="nonstop.streammodel.graphml", packetmodelpath="nonstop.packetmodel.graphml", sendsize="1 kib", recvsize="1 mib")
    G.add_node("end", count="1")

    G.add_edge("start", "flow")
    G.add_edge("flow", "end")
    G.add_edge("end", "start")

    nx.write_graphml(G, "client-nonstop-flow.tgenrc.graphml")

def generate_client_nonstop_stream():
    G = nx.DiGraph()

    G.add_node("start", heartbeat="1 second", loglevel="message", peers="localhost:8888")
    G.add_node("stream", timeout="0 seconds", markovmodelseed="12345", packetmodelpath="nonstop.packetmodel.graphml", packetmodelmode="graphml")
    G.add_node("end", count="1")

    G.add_edge("start", "stream")
    G.add_edge("stream", "end")
    G.add_edge("end", "start")

    nx.write_graphml(G, "client-nonstop-stream.tgenrc.graphml")

if __name__ == '__main__': sys.exit(main())
