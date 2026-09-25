#!/bin/bash
set -e

DATA_DIR="/app/data"
REQUIRED_FILES=("hanoi_ch.bin" "snap_tree.bin" "database.sqlite")

echo "[init] Kiểm tra dữ liệu bản đồ..."

ALL_PRESENT=true
for f in "${REQUIRED_FILES[@]}"; do
    if [ ! -f "$DATA_DIR/$f" ]; then
        ALL_PRESENT=false
        echo "[init] Thiếu file: $f"
    fi
done

if [ "$ALL_PRESENT" = true ]; then
    echo "[init] Đủ dữ liệu, bỏ qua bước tải."
else
    if [ -n "$DATA_URL" ]; then
        FILENAME=$(basename "$DATA_URL")
        echo "[init] Đang tải dữ liệu từ: $DATA_URL"
        wget -q --show-progress -O "/tmp/$FILENAME" "$DATA_URL"
        echo "[init] Giải nén vào $DATA_DIR..."
        mkdir -p "$DATA_DIR"
        
        if [[ "$FILENAME" == *.7z ]]; then
            7z x "/tmp/$FILENAME" -o"$DATA_DIR" -y
        else
            unzip -o "/tmp/$FILENAME" -d "$DATA_DIR" || 7z x "/tmp/$FILENAME" -o"$DATA_DIR" -y
        fi
        rm "/tmp/$FILENAME"
        echo "[init] Tải xong."
    else
        echo "[init] CẢNH BÁO: Thiếu dữ liệu và không có biến DATA_URL."
        echo "[init] Khi test local: copy hanoi_ch.bin, snap_tree.bin, database.sqlite vào ./data/"
        echo "[init] Khi deploy: set DATA_URL trong file .env"
        echo "[init] Khởi động API mà không có dữ liệu (sẽ lỗi khi gọi endpoint routing)..."
    fi
fi

echo "[init] Khởi động FastAPI trên cổng 8000..."
exec uvicorn app.main:app --host 0.0.0.0 --port 8000 --workers 4
