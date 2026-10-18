# MÔ PHỎNG GIAO THỨC CoAP TRONG HỆ THỐNG IoT BẰNG NS-3 VÀ NETANIM

## 1. Giới thiệu

Project xây dựng mô hình mô phỏng giao thức CoAP
(Constrained Application Protocol) trong hệ thống IoT bằng ns-3.47.

Mục tiêu của mô phỏng là trực quan hóa và phân tích các cơ chế truyền
thông quan trọng của CoAP trên nền UDP, bao gồm:

- NON (Non-confirmable Message)
- CON (Confirmable Message)
- ACK
- Retransmission khi mất ACK
- Message ID
- Polling / Pull
- Observe / Push
- Blockwise Transfer
- Mất block và truyền lại block
- Phân tích gói tin bằng Wireshark
- Trực quan hóa quá trình truyền packet bằng NetAnim

Project được xây dựng dựa trên nội dung Bài thực hành số 13:
Giao thức tầng ứng dụng - CoAP của môn Truyền thông trong IoT.

Trong tài liệu thực hành gốc, CoAP Client được triển khai trên
ESP32/ESP8266 và CoAP Server sử dụng Node.js.

Trong project này, hệ thống được chuyển sang môi trường mô phỏng
ns-3 để có thể quan sát rõ topology mạng, đường truyền packet,
mất gói, retransmission và các cơ chế của CoAP.


## 2. Công nghệ sử dụng

- Ubuntu / Linux
- ns-3.47
- C++
- NetAnim
- Wireshark
- UDP
- CoAP
- PCAP


## 3. Kiến trúc hệ thống

Mô hình gồm 7 node:

1. Temperature Sensor
2. Humidity Sensor
3. Gas Sensor
4. Control Center
5. IoT Router
6. CoAP Data Server
7. Firmware Server


Sơ đồ logic:

                 Temperature Sensor
                    CoAP NON
                        \
                         \
                 Humidity Sensor
                    CoAP CON
                          \
                           \
     Gas Sensor -------- IoT Router -------- CoAP Data Server
      Observe               |                   UDP 5683
                            |
                     Control Center
                      GET / Pull
                            |
                            |
                     Firmware Server
                  Blockwise Transfer


## 4. Chức năng từng node

### 4.1 Temperature Sensor

Temperature Sensor mô phỏng cảm biến nhiệt độ.

Node gửi dữ liệu tới CoAP Data Server bằng bản tin NON.

Ví dụ:

POST /sensor/temp

Payload:

temp=30.5

Các Message ID được sử dụng trong mô phỏng:

- MID = 1001
- MID = 1002
- MID = 1003

NON là bản tin Non-confirmable nên phía nhận không bắt buộc phải gửi
ACK.

Kết quả mô phỏng:

2s TEMP SENSOR -> NON MID=1001 temp=30.5

2.00809s DATA SERVER nhan NON MID=1001

NON -> KHONG CAN ACK


## 5. CoAP CON, ACK và Retransmission

Humidity Sensor sử dụng bản tin CON để gửi dữ liệu tới CoAP Data
Server.

Ví dụ:

humidity=72

Message ID:

MID = 2001

Trong lần gửi đầu tiên, Server cố tình không gửi ACK để mô phỏng
trường hợp mất ACK.

Kết quả:

4s HUMIDITY -> CON MID=2001 humidity=72

4.00809s DATA SERVER nhan CON MID=2001 lan=1

SERVER CO TINH LAM MAT ACK

Do Client không nhận được ACK nên thực hiện retransmission.

7s HUMIDITY RETRANSMIT MID=2001

Server nhận lại bản tin:

7.00809s DATA SERVER nhan CON MID=2001 lan=2

Sau đó Server trả ACK:

7.00809s DATA SERVER -> ACK MID=2001

Client nhận:

7.01615s HUMIDITY SENSOR nhan ACK MID=2001


### Nhận xét

Message ID của bản tin retransmission vẫn giữ:

MID = 2001

Điều này cho phép phía nhận nhận biết đây là bản truyền lại của cùng
một CoAP message.


## 6. Polling / Pull

Control Center thực hiện cơ chế Polling bằng cách gửi GET định kỳ tới:

/sensor/temp

Ví dụ:

12s CONTROL CENTER -> GET /sensor/temp MID=5001

Server trả về:

12.0081s DATA SERVER -> PULL RESPONSE MID=5001

Control Center nhận:

12.0161s CONTROL CENTER nhan response MID=5001

Trong mô phỏng có tổng cộng:

PULL GET requests = 5

PULL responses = 5

Đặc điểm của Pull:

- Client chủ động yêu cầu dữ liệu.
- Mỗi lần cần dữ liệu phải gửi một GET request.
- Server trả response tương ứng.
- Nếu polling quá thường xuyên sẽ tạo thêm lưu lượng mạng.


## 7. Observe / Push

Gas Sensor được sử dụng làm Observe Client.

Client gửi yêu cầu đăng ký theo dõi tài nguyên:

/sensor/temp

Kết quả:

13s GAS NODE -> OBSERVE /sensor/temp

Server xác nhận Observer:

13.0081s DATA SERVER: OBSERVER REGISTERED

Sau khi Observe được thiết lập, Server chủ động gửi notification khi
giá trị tài nguyên thay đổi.

Ví dụ:

16s SERVER -> OBSERVE NOTIFY seq=1 temp=30.5

20s SERVER -> OBSERVE NOTIFY seq=2 temp=31.0

24s SERVER -> OBSERVE NOTIFY seq=3 temp=31.5

28s SERVER -> OBSERVE NOTIFY seq=4 temp=32.0

32s SERVER -> OBSERVE NOTIFY seq=5 temp=32.5

Kết quả:

OBSERVE registrations = 1

OBSERVE notifications = 5


### So sánh Pull và Observe

Pull:

Client -> GET -> Server

Server -> Response -> Client

Observe:

Client -> Register Observe -> Server

Sau đó:

Server -> Notification -> Client

Observe phù hợp với các hệ thống giám sát IoT cần nhận dữ liệu khi
trạng thái thay đổi mà không phải gửi GET liên tục.


## 8. Blockwise Transfer

Project mô phỏng truyền một firmware có kích thước:

2048 bytes

Firmware được chia thành:

8 block

Mỗi block:

256 bytes

Các block gồm:

- Block 0
- Block 1
- Block 2
- Block 3
- Block 4
- Block 5
- Block 6
- Block 7

Client gửi yêu cầu từng block tới Firmware Server.

Ví dụ:

CONTROL -> GET FIRMWARE BLOCK 5 MID=6005

Server nhận:

FIRMWARE SERVER request BLOCK 5

Server gửi lại:

FIRMWARE SERVER -> BLOCK 5 size=256 M=1

Client nhận:

CONTROL CENTER nhan FIRMWARE BLOCK packetSize=264


## 9. Ý nghĩa trường M trong Blockwise Transfer

Trong mô phỏng:

M = 1

nghĩa là vẫn còn block tiếp theo.

Ví dụ:

BLOCK 5 size=256 M=1

BLOCK 6 size=256 M=1

Block cuối cùng:

BLOCK 7 size=256 M=0

M = 0 cho biết đây là block cuối của dữ liệu.


## 10. Mô phỏng mất Block

Project cố tình làm mất Block 4.

Client sau đó yêu cầu lại Block 4.

Điều này mô phỏng một đặc điểm quan trọng của Blockwise Transfer:

Nếu một block bị mất, Client chỉ cần yêu cầu lại block bị mất thay vì
phải truyền lại toàn bộ firmware.

Kết quả thống kê:

BLOCK requests = 9

BLOCK responses = 8

BLOCK retransmissions = 1


## 11. Kết quả thống kê

Kết quả cuối của mô phỏng:

| Thông số | Giá trị |
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


## 12. NetAnim

ns-3 tạo file:

coap-full.xml

File này được mở bằng NetAnim để trực quan hóa topology và quá trình
truyền packet.

Chạy NetAnim:

```bash
~/netanim/build/netanim
