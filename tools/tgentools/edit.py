'''
  tgentools
  Authored by Rob Jansen, 2015
  See LICENSE for licensing information
'''

import re, logging

from networkx import read_graphml, write_graphml

def edit_config(path, node_pattern, edge_source_pattern, edge_target_pattern, name, value):
    logging.info("Reading config file at path '{}'".format(path))
    G = read_graphml(path)

    # first go through all nodes and match the node name
    if node_pattern != None:
        logging.info("Searching nodes using pattern '{}'".format(node_pattern))

        num_nodes_changed = 0

        for n in G.nodes():
            if re.search(node_pattern, n):
                logging.info("Setting {}={} for node {}".format(name, value, n))
                G.node[n][name] = value
                num_nodes_changed += 1

        logging.info("Done searching, we edited {} nodes".format(num_nodes_changed))

    # now go through all nodes and match the name of the source or destination node
    if edge_source_pattern != None or edge_target_pattern != None:
        logging.info("Searching edges using source pattern " + \
            "'{}' and target pattern '{}'".format(edge_source_pattern, edge_target_pattern))

        num_edges_changed = 0

        for (src, dst) in G.edges():
            should_edit = False

            if edge_source_pattern != None and edge_target_pattern != None:
                if re.search(edge_source_pattern, src) and re.search(edge_target_pattern, dst):
                    should_edit = True
            elif edge_source_pattern != None:
                if re.search(edge_source_pattern, src):
                    should_edit = True
            elif edge_target_pattern != None:
                if re.search(edge_target_pattern, dst):
                    should_edit = True

            if should_edit:
                logging.info("Setting {}={} for edge {}-{}".format(name, value, src, dst))
                G[src][dst][name] = value
                num_edges_changed += 1

        logging.info("Done searching, we edited {} edges".format(num_edges_changed))

    logging.info("Writing edited config file at path '{}'".format(path))

    write_graphml(G, path)
