# ==========================================
# STAGE 1: Builder
# ==========================================
FROM python:3.10-slim AS builder

RUN apt-get update && apt-get install -y \
    cmake \
    g++ \
    make \
    libsqlite3-dev \
    zlib1g-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY CMakeLists.txt .
COPY src/ src/
COPY include/ include/

RUN cmake -B build -DCMAKE_BUILD_TYPE=Release

# Chỉ build target routing_engine (.so), không build Parser (không cần khi runtime)
RUN cmake --build build --config Release --target routing_engine

# ==========================================
# STAGE 2: Runner
# ==========================================
FROM python:3.10-slim AS runner

# Runtime: chỉ cần libsqlite3-0 (bản chạy, không có compiler)
RUN apt-get update && apt-get install -y \
    libsqlite3-0 \
    wget \
    unzip \
    p7zip-full \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt

# Copy file .so từ Stage 1
COPY --from=builder /app/build/routing_engine*.so ./build/

# Copy mã nguồn Python và script khởi tạo
COPY app/ app/
COPY init-data.sh .

RUN chmod +x init-data.sh

EXPOSE 8000

CMD ["./init-data.sh"]
