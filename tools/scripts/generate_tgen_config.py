#!/usr/bin/python

import sys
import networkx as nx

def main():
    generate_tgen_server()
    generate_tgen_client()
    generate_tgen_nonstop_client()
    generate_tgen_perf_clients(size="50 KiB", name="client50k.tgenrc.graphml")
    generate_tgen_perf_clients(size="1 MiB", name="client1m.tgenrc.graphml")
    generate_tgen_perf_clients(size="5 MiB", name="client5m.tgenrc.graphml")

def generate_tgen_server():
    G = nx.DiGraph()
    G.add_node("start", serverport="8888", heartbeat="1 second", loglevel="message")
    nx.write_graphml(G, "server.tgenrc.graphml")

def generate_tgen_client():
    G = nx.DiGraph()

    G.add_node("start", time="1 second", heartbeat="1 second", loglevel="message", peers="localhost:8888")

    G.add_node("streamA", sendsize="1 kib", recvsize="10 kib")
    G.add_node("streamB1", sendsize="10 kib", recvsize="10 mib")
    G.add_node("streamB2", sendsize="100 kib", recvsize="100 mib")

    G.add_node("pause_sync")
    G.add_node("pause", time="1,2,3,4,5,6,7,8,9,10")
    G.add_node("end", time="1 minute", count="30", recvsize="10 GiB", sendsize="10 GiB")

    # a small first round request
    G.add_edge("start", "streamA")
    # two second round requests in parallel
    G.add_edge("streamA", "streamB1")
    G.add_edge("streamA", "streamB2")
    # wait for both second round streams to finish
    G.add_edge("streamB1", "pause_sync")
    G.add_edge("streamB2", "pause_sync")
    # check if we should stop
    G.add_edge("pause_sync", "end")
    # pause for a short time and then start again
    G.add_edge("end", "pause")
    G.add_edge("pause", "start")

    nx.write_graphml(G, "client.tgenrc.graphml")

def generate_tgen_nonstop_client():
    G = nx.DiGraph()

    G.add_node("start", time="1 second", heartbeat="1 second", loglevel="message", peers="localhost:8888")

    # no configured packet model, tgen will use a nonstop infinite model
    G.add_node("stream")

    # the stream will never end unless an error occurs
    # if that happens, we do not try again
    G.add_node("end", count="1")

    G.add_edge("start", "stream")
    G.add_edge("stream", "end")
    G.add_edge("end", "start")

    nx.write_graphml(G, "clientnonstop.tgenrc.graphml")

def generate_tgen_perf_clients(servers="localhost:8888", size="50 KiB", name="client50k.tgenrc.graphml"):
    G = nx.DiGraph()

    G.add_node("start", peers=servers)
    G.add_node("stream", sendsize="1 kib", recvsize=size)
    G.add_node("pause", time="60")

    G.add_edge("start", "stream")
    G.add_edge("stream", "pause")
    G.add_edge("pause", "start")

    nx.write_graphml(G, name)

if __name__ == '__main__': sys.exit(main())
