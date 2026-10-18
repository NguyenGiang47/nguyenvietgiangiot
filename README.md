# TRUYỀN THÔNG TRONG IoT

## Giới thiệu

Repository này tổng hợp các bài thực hành trong môn **Truyền thông trong IoT**.

Nội dung tập trung vào quá trình kết nối thiết bị, truyền nhận dữ liệu, xây dựng hệ thống IoT và mô phỏng các giao thức truyền thông trên nhiều nền tảng khác nhau.

Các công nghệ chính được sử dụng gồm **ESP32-S3, Bluetooth BLE, Wi-Fi, MQTT, Home Assistant, ESPHome, Zigbee, Zigbee2MQTT, CoAP, ns-3, NetAnim và Wireshark**.

---

## Nội dung thực hành

| Thư mục | Nội dung chính |
|---|---|
| `BAI1_TT` | Các bài thực hành nền tảng về truyền thông |
| `BAI2_TT` | Thực hành truyền và nhận dữ liệu |
| `BAI2.1_TT` | Phần mở rộng của bài truyền thông |
| `BÀI BLT` | Bluetooth Low Energy với ESP32-S3 |
| `BÀI HA` | Home Assistant, Debian, MQTT và Mosquitto |
| `BÀI WIFI` | ESP32-S3, Wi-Fi và ESPHome |
| `BÀI ZIGBEE` | Zigbee, Zigbee2MQTT và Home Assistant |
| `CoAP` | Mô phỏng CoAP bằng ns-3.47, NetAnim và Wireshark |

---

# 1. Bluetooth Low Energy - BLE

Bài thực hành BLE sử dụng **ESP32-S3** làm thiết bị giao tiếp và điện thoại sử dụng ứng dụng **nRF Connect** để gửi dữ liệu.

Mô hình:

```text
Điện thoại / nRF Connect
          │
          │ BLE
          ▼
      ESP32-S3
          │
          ▼
         LED
```

ESP32-S3 nhận lệnh từ điện thoại:

```text
1 → Bật LED
0 → Tắt LED
```

Qua bài thực hành có thể làm quen với BLE Server, Client, Service, Characteristic và truyền dữ liệu không dây giữa điện thoại với ESP32-S3.

---

# 2. Home Assistant và MQTT

Home Assistant được triển khai trên **Debian Linux chạy trong Oracle VirtualBox** và được sử dụng làm trung tâm quản lý hệ thống IoT.

Mô hình:

```text
Thiết bị IoT
     │
     │ MQTT / Wi-Fi / Zigbee
     ▼
Mosquitto Broker
     │
     ▼
Home Assistant
     │
     ▼
  Dashboard
```

Các nội dung thực hành gồm cài đặt Home Assistant, quản lý Device và Entity, sử dụng **Mosquitto MQTT Broker**, tích hợp MQTT và theo dõi thiết bị trên Dashboard.

MQTT hoạt động theo mô hình:

```text
Publisher → MQTT Broker → Subscriber
```

---

# 3. Wi-Fi và ESPHome

ESP32-S3 được kết nối vào mạng Wi-Fi và tích hợp với Home Assistant thông qua **ESPHome**.

Mô hình:

```text
ESP32-S3
    │
    │ Wi-Fi
    ▼
Router / Access Point
    │
    ▼
Home Assistant
    │
    ▼
ESPHome Integration
```

ESPHome được sử dụng để cấu hình thiết bị, kết nối Wi-Fi, biên dịch firmware, nạp firmware qua USB và cập nhật OTA sau khi thiết bị đã hoạt động trên mạng.

ESPHome Native API cho phép ESP32-S3 trao đổi dữ liệu trực tiếp với Home Assistant.

Wireshark có thể được sử dụng để quan sát các giao thức liên quan như:

```text
DHCP
ARP
mDNS
ESPHome API
OTA
```

---

# 4. Zigbee và Zigbee2MQTT

Bài Zigbee xây dựng mô hình kết nối thiết bị Zigbee với Home Assistant.

Mô hình:

```text
Zigbee Device
      │
      │ Zigbee
      ▼
Zigbee Coordinator
      │
      ▼
Zigbee2MQTT
      │
      │ MQTT
      ▼
Mosquitto Broker
      │
      ▼
Home Assistant
```

Coordinator sử dụng **Sonoff Zigbee 3.0 USB Dongle Plus V2**.

Zigbee2MQTT làm cầu nối giữa mạng Zigbee và MQTT.

Qua bài thực hành có thể pair thiết bị Zigbee, theo dõi trạng thái thiết bị, kiểm tra **LQI - Link Quality Indicator** và đưa thiết bị vào Home Assistant để giám sát hoặc điều khiển.

---

# 5. CoAP - Constrained Application Protocol

Phần CoAP được xây dựng dưới dạng **mô phỏng mạng bằng ns-3.47** trên Ubuntu.

Các công cụ sử dụng:

```text
Ubuntu
ns-3.47
C++
NetAnim
Wireshark
UDP
CoAP
PCAP
```

Mô hình gồm 7 node:

```text
 TEMP SENSOR - NON ───────────┐
                              │
 HUMIDITY SENSOR - CON ───────┤
                              │
 GAS SENSOR - OBSERVE ────────┼── IoT ROUTER ─── CoAP DATA SERVER
                              │                         UDP 5683
 CONTROL CENTER - PULL ───────┤
                              │
                              └─────────────── FIRMWARE SERVER
```

Mô phỏng thực hiện nhiều cơ chế quan trọng của CoAP.

### NON - Non-confirmable

Temperature Sensor gửi dữ liệu mà không yêu cầu ACK.

```text
TEMP SENSOR → NON
MID = 1001
Payload = temp=30.5
```

Kết quả:

```text
DATA SERVER nhận NON
NON → KHÔNG CẦN ACK
```

### CON - Confirmable

Humidity Sensor gửi bản tin CON:

```text
HUMIDITY → CON
MID = 2001
Payload = humidity=72
```

Server cố tình không gửi ACK ở lần đầu để mô phỏng mất ACK.

Client sau đó retransmission:

```text
HUMIDITY RETRANSMIT
MID = 2001
```

Server nhận lần thứ hai và gửi:

```text
ACK MID=2001
```

Message ID vẫn giữ nguyên để xác định đây là bản truyền lại của cùng một message.

---

# 6. Pull và Observe

Control Center sử dụng cơ chế **Polling / Pull**:

```text
Client → GET /sensor/temp → Server
Client ← Response ← Server
```

Trong khi đó Gas Sensor sử dụng cơ chế **Observe**:

```text
Client → Register Observe → Server

Server → Notification → Client
Server → Notification → Client
Server → Notification → Client
```

Observe cho phép Server chủ động gửi dữ liệu mới sau khi Client đăng ký, thay vì Client phải liên tục gửi GET.

---

# 7. Blockwise Transfer

Mô phỏng còn thực hiện truyền firmware bằng **CoAP Blockwise Transfer**.

Firmware giả lập:

```text
Dung lượng: 2048 bytes
Số block:   8
Mỗi block:  256 bytes
```

Quá trình truyền:

```text
GET Block 0 → Response Block 0
GET Block 1 → Response Block 1
GET Block 2 → Response Block 2
...
GET Block 7 → Response Block 7
```

Các block chưa phải block cuối có:

```text
M = 1
```

Block cuối:

```text
Block 7
M = 0
```

Mô phỏng cố tình làm mất **Block 4**.

Client chỉ yêu cầu truyền lại Block 4 thay vì tải lại toàn bộ firmware.

---

# 8. Kết quả mô phỏng CoAP

| Thông số | Kết quả |
|---|---:|
| NON sent | 3 |
| CON sent | 1 |
| CON retransmission | 1 |
| ACK received | 1 |
| Pull GET requests | 5 |
| Pull responses | 5 |
| Observe registrations | 1 |
| Observe notifications | 5 |
| Block requests | 9 |
| Block responses | 8 |
| Block retransmissions | 1 |

---

# 9. NetAnim

Sau khi mô phỏng CoAP hoàn thành, ns-3 tạo:

```text
coap-full.xml
```

Mở NetAnim bằng:

```bash
~/netanim/build/netanim
```

Sau đó mở file:

```text
coap-full.xml
```

NetAnim cho phép quan sát trực quan topology 7 node và các packet truyền qua IoT Router giữa Client và Server.

---

# 10. Wireshark

ns-3 đồng thời tạo các file `.pcap` để phân tích bằng Wireshark.

Có thể sử dụng bộ lọc:

```text
coap
```

hoặc:

```text
udp.port == 5683
```

để quan sát Message Type, Message ID, Token, URI, Payload, Observe, Blockwise và các gói UDP.

---

# 11. Tổng hợp công nghệ

| Công nghệ | Vai trò |
|---|---|
| ESP32-S3 | Vi điều khiển IoT |
| BLE | Truyền thông không dây khoảng cách gần |
| Wi-Fi | Kết nối thiết bị vào mạng IP |
| MQTT | Publish / Subscribe |
| Mosquitto | MQTT Broker |
| Home Assistant | Quản lý và điều khiển IoT |
| ESPHome | Tích hợp ESP32 với Home Assistant |
| Zigbee | Mạng IoT công suất thấp |
| Zigbee2MQTT | Cầu nối Zigbee ↔ MQTT |
| CoAP | Giao thức ứng dụng nhẹ cho IoT |
| UDP | Giao thức vận chuyển |
| ns-3 | Mô phỏng mạng |
| NetAnim | Trực quan hóa mô phỏng |
| Wireshark | Phân tích packet |

---

# 12. Kiến thức đạt được

Qua các bài thực hành, repository thể hiện quá trình từ kết nối thiết bị IoT cơ bản đến xây dựng và phân tích hệ thống truyền thông hoàn chỉnh:

```text
BLE
 ↓
Wi-Fi
 ↓
MQTT
 ↓
Home Assistant
 ↓
ESPHome
 ↓
Zigbee / Zigbee2MQTT
 ↓
CoAP
 ↓
ns-3 / NetAnim / Wireshark
```

Qua đó có thể hiểu cách thiết bị IoT kết nối, truyền dữ liệu, xác nhận bản tin, xử lý mất gói, cập nhật dữ liệu và giao tiếp với hệ thống quản lý trung tâm.

---

# Tác giả

**Nguyễn Viết Giang**

Sinh viên K22 - HYUTE

**Môn học: Truyền thông trong IoT**
