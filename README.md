# Routing Engine - Setup & Usage Guide

This project is a custom C++ Routing Engine utilizing Contraction Hierarchies (CH) and STR-Tree to optimize routing queries on real-world map data.

This guide provides instructions to build and run the project from a fresh clone, requiring only an `.osm.pbf` map data file.

For detailed technical architecture and data flows, please refer to [structure.md](structure.md).

---

## 1. Prerequisites

Ensure the following tools are installed on your system:
- C++ Compiler: Visual Studio (MSVC) with C++ workload for Windows, or GCC/Clang for Linux/macOS.
- CMake: Version 3.10 or higher.
- Python: Version 3.7 or higher (Python 3.10+ recommended).

---

## 2. Quick Start

### Step 1: Build the project with CMake

Open a terminal at the project root directory (`Google Maps/`) and execute the following commands to build the C++ executable and Python bindings (.pyd):

```bash
# Initialize build directory
cmake -B build

# Compile source code (Release mode for maximum performance)
cmake --build build --config Release
```

Upon successful compilation, the following files will be generated:
- Preprocessing executable: `build/Release/RoutingEngine_Parser.exe`
- Python library (Pybind11 module): `build/Release/routing_engine.*.pyd`

### Step 2: Prepare map data

Download an OpenStreetMap data file (.osm.pbf). For example, the Hanoi dataset (`hanoi.osm.pbf`).
Data can be obtained from providers like [Geofabrik](http://download.geofabrik.de/) or [BBBike](https://extract.bbbike.org/).

Copy the downloaded `.osm.pbf` file to the project root directory and rename it to `hanoi.osm.pbf` (or any preferred name, provided it matches the filename used in Step 3).

### Step 3: Data Preprocessing

Execute the pre-built binary to preprocess the `.osm.pbf` file.
This step includes: OSM data parsing, POI/street extraction to SQLite, graph construction, Contraction Hierarchies (CH) calculation, binary serialization, and STR-Tree spatial index building.

Run the following command:

```bash
# Replace hanoi.osm.pbf with your actual filename if different
./build/Release/RoutingEngine_Parser.exe hanoi.osm.pbf
```

*(This process may take from a few minutes to tens of minutes depending on the map size).*

Once completed, a `data/` directory will be created containing:
1. `database.sqlite`: SQLite database storing streets and POIs for Geocoding.
2. `hanoi_ch.bin`: Optimized binary CH graph.
3. `snap_tree.bin`: STR-Tree spatial index for snapping GPS coordinates to the road network.

### Step 4: Run Routing Queries via Python

The engine is now ready. The project includes test scripts in the `test/` directory for immediate querying.

The test scripts are configured to automatically append `build/Release` to `sys.path` to locate the compiled `.pyd` module.

Run the following commands:

```bash
# Test routing queries
python test/test_routing.py

# Test snapping GPS coordinates to the nearest edge
python test/test_snap.py
```

---

## 3. Integrating the Routing Engine into Python Code

To use the engine in a new Python script, ensure the `routing_engine` module is imported and the generated data files are loaded.

Example reference (`example.py`):

```python
import sys
import os

# Append the path containing the .pyd library if running from the root directory
sys.path.append(os.path.abspath(os.path.join(os.path.dirname(__file__), "build", "Release")))

import routing_engine

def main():
    print("Initializing Engine...")
    
    # 1. Load CH Graph
    graph = routing_engine.CHGraphQuery()
    if not routing_engine.load_graph_query(graph, "data/hanoi_ch.bin"):
        print("Failed to load graph!")
        return
        
    # 2. Load Snap Tree (STR-Tree)
    snap = routing_engine.SnapTree()
    if not routing_engine.load_snap_tree(snap, "data/snap_tree.bin"):
        print("Failed to load snap tree!")
        return
        
    print("Data loaded successfully!")
    
    # --- ROUTING TEST ---
    # Coordinates for A and B (Example: Hoan Kiem Lake -> Hanoi University of Science and Technology)
    lat_A, lon_A = 21.0278, 105.8342
    lat_B, lon_B = 21.0045, 105.8440
    
    # Snap GPS coordinates to the graph
    snap_from = routing_engine.snap_nearest(snap, graph, lat_A, lon_A)
    snap_to   = routing_engine.snap_nearest(snap, graph, lat_B, lon_B)
    
    if snap_from.valid and snap_to.valid:
        # Find shortest path
        route = routing_engine.find_shortest_path_ch(
            graph, 
            snap_from.edge_u, 
            snap_to.edge_u, 
            0x01 # MODE_CAR
        )
        
        if route.valid:
            print(f"Distance: {route.distance_m / 1000.0:.2f} km")
            print(f"Time    : {route.time_s / 60.0:.1f} minutes")
        else:
            print("No route found.")

if __name__ == "__main__":
    main()
```
