
# LAB #3: XÂY DỰNG HỆ THỐNG PHÁT HIỆN TÉ NGÃ SỬ DỤNG ESP32, FREERTOS VÀ MQTT

**Họ tên:** Nguyễn Quỳnh Anh  
**Lớp:** Kỹ sư Kỹ thuật Viễn thông – Khóa 64

## Mô tả

ESP32 kết hợp cảm biến MPU6050 để thu thập dữ liệu gia tốc và góc nghiêng, phát hiện té ngã. Khi phát hiện té ngã, hệ thống tự động bật LED và buzzer cảnh báo. Dữ liệu được gửi lên MQTT Broker (broker.emqx.io) và hiển thị trên Website, hỗ trợ giám sát và điều khiển từ xa.

## Kiến trúc FreeRTOS

| Task | Core | Nhiệm vụ |
|---|---|---|
| TaskTransmitData | 0 | Kết nối WiFi/MQTT, gửi dữ liệu và nhận lệnh từ Web |
| TaskReadData | 1 | Đọc MPU6050, tính Acc, Roll, Pitch và phát hiện té ngã |
| TaskAlert | 1 | Xử lý cảnh báo, điều khiển LED và buzzer |

## Sơ đồ chân

- MPU6050 SDA: GPIO21, SCL: GPIO22.
- LED cảnh báo: GPIO2.
- Buzzer: GPIO25.

## Thuật toán phát hiện té ngã

- Gia tốc tổng hợp đạt 2,34 g.
- Góc Roll hoặc Pitch vượt quá 50°.
- Duy trì góc bất thường ít nhất 500 ms trong khoảng xác nhận 3 giây.

## Cấu trúc thư mục

- `7-11.ino`: Code ESP32.
- `1.html`: Giao diện Website.
- `ESP32_MPU6050.docx`: Báo cáo.
- `images/`: Ảnh kết quả.

## Link Wokwi

https://wokwi.com/projects/478024918068225025
