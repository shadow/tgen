#!/usr/bin/env python

import networkx

def main():
    nonstop = "nonstop.packetmodel.graphml"
    generate_nonstop_packetmodel(nonstop)
    print_as_c_string(nonstop)

def generate_nonstop_packetmodel(filename):
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

    # '+', '-', '$', and 'F' are special keywords
    # '+': packet from client to server
    # '-': packet from server to client
    # '$': create new stream
    # observations must use one of these keywords
    # you should not mix packet and stream creations in the same model
    G.add_node('o1', type="observation", name='+')
    G.add_node('o2', type="observation", name='-')

    G.add_edge('s1', 'o1', type='emission', weight=0.5, lognorm_mu=0.0, lognorm_sigma=0.0, exp_lambda=4294967295)
    G.add_edge('s1', 'o2', type='emission', weight=0.5, lognorm_mu=0.0, lognorm_sigma=0.0, exp_lambda=4294967295)

    networkx.write_graphml(G, filename)

def print_as_c_string(filename):
    with open(filename, 'r') as inf:
        for line in inf:
            escaped = line.replace('"', '\\"')
            print '"{}"'.format(escaped.rstrip())

if __name__ == "__main__":
    main()
