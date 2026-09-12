#pragma once
#include "graph.h"

// Run Contraction Hierarchies preprocessing on the graph
// - Assigns rank to each node
// - Generates shortcut edges
// - transport_mode_filter: only consider edges matching this bitmask
//   (e.g. MODE_CAR | MODE_MOTORBIKE for vehicle routing)
void runContraction(CHGraph& graph, uint8_t transport_mode_filter);
