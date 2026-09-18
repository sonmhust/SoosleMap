# Hướng Dẫn Upload Dữ Liệu Bản Đồ Lên Cloudflare R2

Thực hiện bước này sau khi đã test Docker thành công trên máy local.
Cloudflare R2 được ưu tiên hơn AWS S3 vì miễn phí 10GB lưu trữ và không tính phí download (egress).

## Bước 1: Tạo tài khoản Cloudflare

Truy cập https://cloudflare.com và đăng ký tài khoản miễn phí. Không cần thẻ tín dụng.

## Bước 2: Tạo R2 Bucket

1. Vào Cloudflare Dashboard > R2 Object Storage > Create Bucket
2. Đặt tên bucket (ví dụ: `routing-engine-data`)
3. Chọn Region: Automatic (Cloudflare tự chọn vị trí gần nhất)

## Bước 3: Nén file dữ liệu

Mở terminal tại thư mục `data/` của dự án:

```powershell
# Trên Windows (PowerShell)
Compress-Archive -Path hanoi_ch.bin, snap_tree.bin, database.sqlite -DestinationPath map-data.zip
```

```bash
# Trên Linux/Mac
zip map-data.zip hanoi_ch.bin snap_tree.bin database.sqlite
```

## Bước 4: Upload lên R2

1. Vào bucket vừa tạo > Upload > chọn file `map-data.zip`
2. Đợi upload hoàn tất

## Bước 5: Bật Public Access

1. Vào bucket > Settings > Public Access
2. Bật "Allow Access" và kết nối subdomain (R2 cấp subdomain dạng `pub-xxxx.r2.dev`)
3. Copy Public URL của file, dạng:
   ```
   https://pub-xxxx.r2.dev/map-data.zip
   ```

## Bước 6: Cập nhật file .env

Mở file `.env` (tạo từ `.env.example`), điền URL vào:

```env
DATA_URL=https://pub-xxxx.r2.dev/map-data.zip
```

Từ lần sau khi Oracle VM khởi động Container, `init-data.sh` sẽ tự động tải file về qua biến `DATA_URL` này.

## So sánh R2 vs S3 (Lý do chọn R2)

| Tiêu chí | Cloudflare R2 | AWS S3 Free Tier |
|---|---|---|
| Lưu trữ miễn phí | 10 GB/tháng | 5 GB/tháng |
| Phí download (egress) | Miễn phí | $0.09/GB |
| Cần thẻ tín dụng | Không | Có |
| Tốc độ tải | Nhanh (CDN toàn cầu) | Phụ thuộc Region |

---

# Hướng Dẫn Tạo Oracle Cloud VM (Always Free)

## Bước 1: Đăng ký tài khoản Oracle Cloud

Truy cập https://cloud.oracle.com và đăng ký. Cần thẻ tín dụng để xác minh danh tính nhưng sẽ không bị tính phí nếu chỉ dùng tài nguyên Always Free.

## Bước 2: Tạo Compute Instance (VM)

1. Vào Oracle Cloud Console > Compute > Instances > Create Instance
2. Cấu hình:
   - Name: `routing-engine-vm`
   - Image: Oracle Linux 8 hoặc Ubuntu 22.04
   - Shape: Chọn **Ampere** (ARM) > `VM.Standard.A1.Flex`
   - Số vCPU: 4, RAM: 24 GB (giới hạn Always Free cho toàn tài khoản)
3. Networking: Giữ mặc định VCN và Subnet
4. SSH Keys: Upload public key của bạn hoặc để Oracle tự sinh rồi tải về file `.pem`
5. Boot Volume: 50 GB (miễn phí)
6. Bấm Create

## Bước 3: Mở Port trên Security List

1. Vào VCN > Security Lists > Default Security List
2. Add Ingress Rule:
   - Source CIDR: `0.0.0.0/0`
   - Protocol: TCP
   - Destination Port: `80` (HTTP cho Nginx)
3. Thêm rule tương tự cho Port `443` (HTTPS nếu cần)

## Bước 4: SSH vào VM và Cài Docker

```bash
# SSH vào VM (thay <IP> bằng Public IP của VM)
ssh -i <key.pem> ubuntu@<IP>

# Cài Docker
curl -fsSL https://get.docker.com | sh
sudo usermod -aG docker $USER
newgrp docker

# Cài Docker Compose
sudo curl -L "https://github.com/docker/compose/releases/latest/download/docker-compose-$(uname -s)-$(uname -m)" -o /usr/local/bin/docker-compose
sudo chmod +x /usr/local/bin/docker-compose
```

## Bước 5: Clone project và chạy

```bash
git clone <your-repo-url>
cd <project-folder>

# Tạo .env từ template
cp .env.example .env
# Mở .env, điền DATA_URL

# Build và chạy
docker-compose up --build -d
```

Kiểm tra API hoạt động:
```bash
curl http://<IP>/health
```

---

# Hướng Dẫn Tạo Oracle Kubernetes Engine (OKE)

Thực hiện bước này khi muốn mở rộng hệ thống lên nhiều VM (Scale Up). Không cần thiết cho giai đoạn cá nhân/CV.

## Điều kiện tiên quyết

- Đã có Oracle Cloud Account
- Đã cài `kubectl` và `oci` CLI trên máy local
- Docker Image của dự án đã được push lên Docker Hub hoặc GitHub Container Registry (GHCR)

## Bước 1: Push Docker Image lên Registry

```bash
# Đăng nhập Docker Hub
docker login

# Tag và push image
docker build -t <your-dockerhub-username>/routing-engine:latest .
docker push <your-dockerhub-username>/routing-engine:latest
```

## Bước 2: Tạo OKE Cluster

1. Vào Oracle Cloud Console > Developer Services > Kubernetes Clusters (OKE) > Create Cluster
2. Chọn **Quick Create**
3. Cấu hình:
   - Name: `routing-engine-cluster`
   - Kubernetes Version: Mới nhất
   - Node Pool Shape: `VM.Standard.A1.Flex` (ARM, Always Free)
   - Số Node: 1 (Free Tier giới hạn 4 vCPU / 24GB RAM tổng)
4. Bấm Create

## Bước 3: Kết nối kubectl với Cluster

```bash
# Cài OCI CLI (nếu chưa có)
bash -c "$(curl -L https://raw.githubusercontent.com/oracle/oci-cli/master/scripts/install/install.sh)"

# Lấy kubeconfig
oci ce cluster create-kubeconfig --cluster-id <cluster-ocid> --file ~/.kube/config --region ap-singapore-1 --token-version 2.0.0

# Kiểm tra kết nối
kubectl get nodes
```

## Bước 4: Deploy ứng dụng lên K8s

Tạo file `k8s-deployment.yaml`:

```yaml
apiVersion: apps/v1
kind: Deployment
metadata:
  name: routing-engine
spec:
  replicas: 2
  selector:
    matchLabels:
      app: routing-engine
  template:
    metadata:
      labels:
        app: routing-engine
    spec:
      containers:
      - name: api
        image: <your-dockerhub-username>/routing-engine:latest
        ports:
        - containerPort: 8000
        env:
        - name: DATA_URL
          value: "https://pub-xxxx.r2.dev/map-data.zip"
        - name: REDIS_URL
          value: "redis://redis-service:6379"
---
apiVersion: v1
kind: Service
metadata:
  name: routing-engine-service
spec:
  type: LoadBalancer
  selector:
    app: routing-engine
  ports:
  - port: 80
    targetPort: 8000
```

```bash
kubectl apply -f k8s-deployment.yaml
kubectl get services  # Lấy External IP của LoadBalancer
```
