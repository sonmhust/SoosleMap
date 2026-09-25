# pyrefly: ignore [missing-import]
from fastapi import FastAPI, HTTPException
# pyrefly: ignore [missing-import]
from fastapi.staticfiles import StaticFiles
# pyrefly: ignore [missing-import]
from fastapi.responses import FileResponse
# pyrefly: ignore [missing-import]
from fastapi.middleware.cors import CORSMiddleware
# pyrefly: ignore [missing-import]
from pydantic import BaseModel
import sys
import os
import json
import redis

DATA_DIR = os.environ.get("DATA_DIR", "/app/data")

sys.path.append(os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "build")))

try:
    # pyrefly: ignore [missing-import]
    import routing_engine
except ImportError as e:
    print(f"[Warning] routing_engine not found: {e}. OK nếu đang dev trên Windows.")
    routing_engine = None

app = FastAPI(
    title="Routing Engine API",
    description="C++ Routing Engine (Contraction Hierarchies) wrapped via Pybind11",
    version="1.0.0"
)

# CORS cho phép local dev (browser gọi thẳng API không qua Nginx)
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_methods=["*"],
    allow_headers=["*"],
)

# Serve frontend HTML tại /static/
STATIC_DIR = os.path.join(os.path.dirname(__file__), "static")
if os.path.isdir(STATIC_DIR):
    app.mount("/static", StaticFiles(directory=STATIC_DIR), name="static")

# Các biến toàn cục nạp lên RAM một lần duy nhất lúc startup
graph = None
snap = None
geocoder = None
cache: redis.Redis = None


@app.on_event("startup")
def load_data():
    global graph, snap, geocoder, cache

    # --- Kết nối Redis (Application Cache) ---
    redis_url = os.environ.get("REDIS_URL", "redis://localhost:6379")
    try:
        cache = redis.from_url(redis_url, decode_responses=True)
        cache.ping()
        print(f"[startup] Redis connected: {redis_url}")
    except Exception as e:
        print(f"[startup] Redis unavailable: {e}. Tiếp tục không có cache.")
        cache = None

    if routing_engine is None:
        print("[startup] routing_engine không có, bỏ qua nạp dữ liệu.")
        return

    # --- Nạp CH Graph lên RAM ---
    try:
        graph = routing_engine.CHGraphQuery()
        graph_path = os.path.join(DATA_DIR, "hanoi_ch.bin")
        if not routing_engine.load_graph_query(graph, graph_path):
            raise RuntimeError(f"Không thể nạp {graph_path}")
        print(f"[startup] CH Graph loaded from {graph_path}")
    except Exception as e:
        print(f"[startup] Lỗi nạp graph: {e}")
        graph = None

    # --- Nạp STR-Tree lên RAM ---
    try:
        snap = routing_engine.SnapTree()
        snap_path = os.path.join(DATA_DIR, "snap_tree.bin")
        if not routing_engine.load_snap_tree(snap, snap_path):
            raise RuntimeError(f"Không thể nạp {snap_path}")
        print(f"[startup] Snap Tree loaded from {snap_path}")
    except Exception as e:
        print(f"[startup] Lỗi nạp snap tree: {e}")
        snap = None

    # --- Mở kết nối GeocodingDB (SQLite, đọc từ ổ cứng khi cần) ---
    try:
        db_path = os.path.join(DATA_DIR, "database.sqlite")
        geocoder = routing_engine.GeocodingDB(db_path)
        print(f"[startup] GeocodingDB connected: {db_path}")
    except Exception as e:
        print(f"[startup] Lỗi kết nối GeocodingDB: {e}")
        geocoder = None


# ──────────────────────────────────────────────
# Helper: Geocoding với Redis Cache
# ──────────────────────────────────────────────
def geocode_with_cache(query: str) -> dict | None:
    """Chuyển tên POI sang toạ độ. Kiểm tra Redis trước, fallback về SQLite."""
    cache_key = f"poi:{query.lower().strip()}"

    # 1. Thử lấy từ Redis
    if cache:
        cached = cache.get(cache_key)
        if cached:
            return json.loads(cached)

    # 2. Cache Miss: query SQLite qua C++
    if not geocoder:
        return None
    results = geocoder.search_poi(query, limit=1)
    if not results:
        return None

    top = results[0]
    data = {"lat": top.lat, "lon": top.lon, "name": top.name, "type": top.type}

    # 3. Lưu vào Redis (TTL 24h)
    if cache:
        cache.setex(cache_key, 86400, json.dumps(data))

    return data


# ──────────────────────────────────────────────
# Helper: chọn node gần điểm chiếu hơn
# ──────────────────────────────────────────────
def pick_node(snap_result) -> int:
    """Chọn edge_u hoặc edge_v tùy cái nào gần proj_lat/proj_lon hơn.
    Tránh lỗi snap xa khi cạnh dài và điểm click gần edge_v (t ≈ 1.0).
    """
    u, v = snap_result.edge_u, snap_result.edge_v
    du = (graph.get_lat(u) - snap_result.proj_lat) ** 2 \
       + (graph.get_lon(u) - snap_result.proj_lon) ** 2
    dv = (graph.get_lat(v) - snap_result.proj_lat) ** 2 \
       + (graph.get_lon(v) - snap_result.proj_lon) ** 2
    return u if du <= dv else v


def compute_route(lat_a: float, lon_a: float, lat_b: float, lon_b: float, mode: int) -> dict:
    if not graph or not snap:
        raise HTTPException(status_code=503, detail="Engine chưa sẵn sàng hoặc thiếu file data.")

    snap_from = routing_engine.snap_nearest(snap, graph, lat_a, lon_a)
    snap_to   = routing_engine.snap_nearest(snap, graph, lat_b, lon_b)

    if not snap_from.valid or not snap_to.valid:
        raise HTTPException(status_code=400, detail="Toạ độ không nằm trên bản đồ hoặc quá xa đường đi.")

    # Dùng node gần điểm chiếu nhất thay vì luôn dùng edge_u
    start_node = pick_node(snap_from)
    end_node   = pick_node(snap_to)

    route = routing_engine.find_shortest_path_ch(graph, start_node, end_node, mode)

    if not route.valid:
        raise HTTPException(status_code=404, detail="Không tìm thấy đường đi giữa 2 điểm.")

    return {
        "success": True,
        "distance_m": round(route.distance_m, 2),
        "distance_km": round(route.distance_m / 1000.0, 3),
        "time_s": round(route.time_s, 2),
        "time_min": round(route.time_s / 60.0, 2),
        "path_coords": route.path_coords,
        # Tọa độ snap thực tế để frontend hiển thị (debug)
        "snap_from": {"lat": snap_from.proj_lat, "lon": snap_from.proj_lon, "dist_m": round(snap_from.dist_m, 1)},
        "snap_to":   {"lat": snap_to.proj_lat,   "lon": snap_to.proj_lon,   "dist_m": round(snap_to.dist_m, 1)},
    }


# ──────────────────────────────────────────────
# Endpoint 1: Nhận toạ độ trực tiếp (System2System / click bản đồ)
# ──────────────────────────────────────────────
class CoordsRouteRequest(BaseModel):
    lat_a: float
    lon_a: float
    lat_b: float
    lon_b: float
    mode: int = 1  # 1 = MODE_CAR

@app.post("/route/coords", summary="Tìm đường từ toạ độ GPS")
def route_from_coords(req: CoordsRouteRequest):
    return compute_route(req.lat_a, req.lon_a, req.lat_b, req.lon_b, req.mode)


# ──────────────────────────────────────────────
# Endpoint 2: Nhận tên địa điểm (POI) — dùng cho thanh tìm kiếm
# ──────────────────────────────────────────────
class PoiRouteRequest(BaseModel):
    from_poi: str
    to_poi: str
    mode: int = 1

@app.post("/route/poi", summary="Tìm đường từ tên địa điểm (Geocoding + Routing)")
def route_from_poi(req: PoiRouteRequest):
    origin = geocode_with_cache(req.from_poi)
    if not origin:
        raise HTTPException(status_code=404, detail=f"Không tìm thấy địa điểm: '{req.from_poi}'")

    destination = geocode_with_cache(req.to_poi)
    if not destination:
        raise HTTPException(status_code=404, detail=f"Không tìm thấy địa điểm: '{req.to_poi}'")

    result = compute_route(origin["lat"], origin["lon"], destination["lat"], destination["lon"], req.mode)
    result["from"] = origin
    result["to"] = destination
    return result


# ──────────────────────────────────────────────
# Endpoint 3: Tìm kiếm địa điểm (Autocomplete / Suggestion)
# ──────────────────────────────────────────────
@app.get("/search", summary="Tìm kiếm địa điểm (FTS5 Fuzzy Search)")
def search_poi(q: str, limit: int = 10):
    if not geocoder:
        raise HTTPException(status_code=503, detail="GeocodingDB chưa sẵn sàng.")
    results = geocoder.search_poi(q, limit=limit)
    return [{"name": r.name, "type": r.type, "lat": r.lat, "lon": r.lon, "score": r.score} for r in results]


@app.get("/health")
def health():
    return {
        "status": "ok",
        "graph_loaded": graph is not None,
        "snap_loaded": snap is not None,
        "geocoder_loaded": geocoder is not None,
        "redis_connected": cache is not None,
    }


@app.get("/")
def root():
    """Trả về frontend HTML nếu có, fallback về JSON."""
    index_path = os.path.join(STATIC_DIR, "index.html") if os.path.isdir(STATIC_DIR) else None
    if index_path and os.path.isfile(index_path):
        return FileResponse(index_path)
    return {"message": "Routing Engine API. Truy cap /docs de xem Swagger UI."}
