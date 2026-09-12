#pragma once
#include "graph.h"
#include <string>

// Serialize CH graph to binary (SoA CSR format, magic "CHGR").
void serializeGraph(const CHGraph& graph, const std::string& filepath);

// Load binary → CHGraphQuery (for online server & Python).
bool loadGraphQuery(CHGraphQuery& graph, const std::string& filepath);

// Load only lat/lon (lightweight, for snap debugging).
bool loadGraphCoordinates(CHGraphQuery& graph, const std::string& filepath);

// Stub only — AoS format removed.
bool loadGraph(CHGraph& graph, const std::string& filepath);
