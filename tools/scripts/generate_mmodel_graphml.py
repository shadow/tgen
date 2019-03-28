#!/usr/bin/env python

import networkx

def main():
    nonstop = "nonstop.packetmodel.graphml"
    generate_default_packetmodel(nonstop)
    print_as_c_string(nonstop)

    nonstop = "nonstop.streammodel.graphml"
    generate_default_streammodel(nonstop)
    print_as_c_string(nonstop)

def generate_default_packetmodel(filename):
    G = networkx.DiGraph()

    # the "type" and "name" attributes are required on
    # all nodes; the ids (eg 's0') can be whatever you want

    # 'start' is a special keyword and must exist once
    G.add_node('s0', type="state", name='start')
    # non-start states can be given any name
    G.add_node('s1', type="state", name='nonstop')

    # the "type" and "weight" attributes are required on
    # all edges; the ids must match the node ids

    G.add_edge('s0', 's1', type='transition', weight=1.0)
    G.add_edge('s1', 's1', type='transition', weight=1.0)

    # '+', '-', and 'F' are special keywords
    # '+': packet from client to server, or create new stream
    # '-': packet from server to client, or create new stream
    # observations must use one of these keywords
    # you should not mix packet and stream creations in the same model
    G.add_node('o1', type="observation", name='+')
    G.add_node('o2', type="observation", name='-')

    # 'distribution' is required
    # all parameters should be type double
    # if 'exponential', then 'param_rate' is required
    # if 'lognormal', ' then 'param_location' and 'param_scale' are required
    # if 'normal', ' then 'param_location' and 'param_scale' are required
    # if 'pareto', ' then 'param_scale' and 'param_shape' are required
    G.add_edge('s1', 'o1', type='emission', weight=0.5, distribution='exponential', param_rate=100.0)
    G.add_edge('s1', 'o2', type='emission', weight=0.5, distribution='exponential', param_rate=100.0)

    networkx.write_graphml(G, filename)

def generate_default_streammodel(filename):
    G = networkx.DiGraph()

    G.add_node('s0', type="state", name='start')
    G.add_node('s1', type="state", name='default')

    G.add_edge('s0', 's1', type='transition', weight=1.0)
    G.add_edge('s1', 's1', type='transition', weight=1.0)

    G.add_node('o1', type="observation", name='+')

    G.add_edge('s1', 'o1', type='emission', weight=1.0, distribution='normal', param_location=10000000.0, param_scale=4000000.0)

    networkx.write_graphml(G, filename)

def print_as_c_string(filename):
    with open(filename, 'r') as inf:
        for line in inf:
            escaped = line.replace('"', '\\"')
            print '"{}"'.format(escaped.rstrip())

if __name__ == "__main__":
    main()
