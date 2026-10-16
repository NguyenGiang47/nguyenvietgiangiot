BÀI 8.4 – TỰ ĐỘNG BẬT/TẮT LED THEO NHIỆT ĐỘ BẰNG WiFi
Chủ đề: Truyền thông IoT – điều khiển thiết bị qua WiFi bằng ESP32 + ESPHome + Home Assistant.
Nội dung chính: Tự động bật LED khi nhiệt độ mô phỏng vượt 30°C và tắt LED khi giảm xuống dưới 28°C.
1. Giới thiệu bài thực hành
Bài thực hành xây dựng một hệ thống IoT đơn giản: Home Assistant giám sát một giá trị nhiệt độ mô phỏng, sau đó gửi lệnh bật/tắt LED tới ESP32 thông qua ESPHome và mạng WiFi.
Trong bài này, nhiệt độ là giá trị do người dùng thay đổi trên Home Assistant (Input Number), không phải số đo lấy từ cảm biến thật. Việc mô phỏng giúp dễ thử các điều kiện bật/tắt mà không cần làm nóng hoặc làm lạnh cảm biến.
Mục tiêu học tập
– Hiểu vai trò của ESP32, WiFi, ESPHome và Home Assistant trong một hệ thống IoT.
– Biết cách kiểm tra thực thể (entity) điều khiển LED trên Home Assistant.
– Biết cách tạo giá trị số mô phỏng nhiệt độ bằng Helper / Input Number.
– Biết sử dụng Automation, bộ kích hoạt numeric_state và khối hành động choose.
– Kiểm tra và giải thích được hiện tượng LED bật/tắt khi nhiệt độ đi qua hai ngưỡng khác nhau.
2. Thiết bị và phần mềm cần chuẩn bị
Thành phần	Vai trò
ESP32	Vi điều khiển nhận lệnh để điều khiển LED
LED RGB / LED đã cấu hình trong ESPHome	Thiết bị được bật/tắt trong bài
Dây nối, nguồn cấp phù hợp	Kết nối và cấp nguồn theo đúng loại LED / bo mạch sử dụng
Mạng WiFi	Kết nối ESP32 với hệ thống trong mạng nội bộ
Home Assistant	Theo dõi giá trị mô phỏng và thực thi tự động hóa
ESPHome	Cấu hình ESP32, tạo thực thể LED và giao tiếp với Home Assistant
Máy tính / điện thoại có trình duyệt	Mở giao diện Home Assistant để cấu hình và thử nghiệm

Điều kiện trước khi bắt đầu: Home Assistant đã hoạt động; ESP32 đã được nạp cấu hình ESPHome phù hợp; ESP32 kết nối mạng và thực thể LED có thể được bật/tắt thủ công từ Home Assistant.
Chú ý về đấu nối: Ảnh cung cấp không thể hiện đầy đủ sơ đồ chân LED và cấu hình GPIO của ESP32. Vì vậy, tài liệu này không tự gán chân GPIO hoặc sơ đồ đấu dây. Cần dùng đúng sơ đồ mạch và cấu hình ESPHome của thiết bị đang có; không nối trực tiếp LED công suất lớn vào GPIO.
3. Yêu cầu hoạt động
Tình huống	Hành động của hệ thống
Nhiệt độ tăng vượt 30°C	BẬT LED
Nhiệt độ giảm xuống dưới 28°C	TẮT LED
Nhiệt độ nằm giữa hai ngưỡng, hoặc bằng đúng 28°C hay 30°C	Không phát sinh lệnh mới chỉ vì đang ở mức này; LED giữ trạng thái trước đó

Vì sao dùng hai ngưỡng? Ngưỡng bật là 30°C, còn ngưỡng tắt là 28°C. Khoảng chênh 2°C tạo một vùng giữ trạng thái (hysteresis), hạn chế LED bật/tắt liên tục khi giá trị dao động quanh một ngưỡng.
Quan trọng: numeric_state là bộ kích hoạt khi giá trị đi qua ngưỡng, không phải bộ kiểm tra liên tục. Ví dụ, từ 29 → 31°C sẽ kích hoạt bật; nhưng từ 31 → 35°C không kích hoạt bật thêm lần nữa. Giá trị đúng bằng 30°C không thỏa above: 30, và đúng bằng 28°C không thỏa below: 28.
4. Sơ đồ nguyên lý hoạt động
Sơ đồ: Người dùng thay đổi nhiệt độ mô phỏng → Home Assistant (Input Number) → kiểm tra ngưỡng → bật hoặc tắt LED bằng lệnh qua ESPHome/WiFi → ESP32 thực hiện.
Giải thích dễ hiểu: Home Assistant là bộ phận ra quyết định. ESP32 là bộ phận thực hiện lệnh. WiFi là môi trường truyền dữ liệu; ESPHome giúp thiết bị ESP32 trao đổi trạng thái và lệnh với Home Assistant. Trong cấu hình bài này, không cần sử dụng MQTT để thực hiện phần tự động hóa được trình bày.
5. Các thực thể được sử dụng
Trong ảnh thực hành, hai entity_id quan trọng là:
Loại thực thể	Entity ID	Chức năng
Giá trị số mô phỏng	input_number.nhiet_do_mo_phong	Chứa giá trị nhiệt độ do người dùng điều chỉnh
Đèn LED	light.nguyevietgiang_led_rgb_7_mau	Nhận lệnh bật hoặc tắt

Lưu ý: Nếu bạn làm lại trên Home Assistant của mình, entity_id có thể khác. Phải sử dụng ID thực tế của thiết bị và Helper; chỉ đổi tên hiển thị là chưa đủ.
Ảnh 1 – Thực thể LED RGB đã được thêm vào Home Assistant

Ảnh cho thấy thực thể light.nguyevietgiang_led_rgb_7_mau đang được kích hoạt trong Home Assistant.
6. Hướng dẫn thực hiện từng bước
Bước 1 – Kiểm tra kết nối ESP32 và LED
1. Cấp nguồn cho ESP32 và bảo đảm ESP32 kết nối WiFi.
2. Mở Home Assistant → Cài đặt → Thiết bị & dịch vụ và tìm thiết bị ESPHome của bạn.
3. Mở thiết bị, tìm thực thể LED RGB; kiểm tra trạng thái Kích hoạt (Enabled).
4. Thử bật và tắt LED bằng tay từ Home Assistant trước khi làm tự động hóa. Nếu không điều khiển được bằng tay, cần xử lý kết nối/cấu hình ESPHome trước.
5. Ghi lại entity_id thực tế của LED.
Bước 2 – Tạo nhiệt độ mô phỏng
1. Vào Cài đặt → Thiết bị & dịch vụ → Trợ giúp / Helpers (tên mục có thể khác đôi chút giữa các phiên bản).
2. Chọn Tạo trợ giúp / Create helper → Số / Number (Input Number).
3. Đặt tên: Nhiệt độ mô phỏng.
4. Có thể dùng phạm vi 0–60, bước thay đổi 1 và đơn vị °C cho thuận tiện khi thử nghiệm.
5. Lưu lại và xác nhận thực thể tạo ra là input_number.nhiet_do_mo_phong (hoặc thay bằng ID thực tế của bạn).
6. Có thể đưa Helper lên Tổng quan / Dashboard để kéo thanh trượt thay đổi nhiệt độ nhanh hơn.
Bước 3 – Tạo Automation
1. Vào Cài đặt → Tự động hóa & Cảnh → Tự động hóa.
2. Chọn Tạo tự động hóa / Create automation.
3. Đặt tên: Bai 8.4 - Tu dong bat tat LED.
4. Thêm hai bộ kích hoạt kiểu Trạng thái số / Numeric state cho cùng thực thể nhiệt độ:
   - Kích hoạt số 1: above: 30, ID là bat.
   - Kích hoạt số 2: below: 28, ID là tat.
5. Phần Và nếu / Conditions để trống vì bài không yêu cầu điều kiện bổ sung.
6. Trong Thì thực hiện / Actions, thêm khối Chọn / Choose gồm hai lựa chọn:
   - Lựa chọn 1: Nếu kích hoạt có ID bat → gọi light.turn_on cho LED.
   - Lựa chọn 2: Nếu kích hoạt có ID tat → gọi light.turn_off cho LED.
7. Lưu tự động hóa và bảo đảm công tắc của nó đang Bật / Enabled.
Ảnh 2 – Giao diện cài đặt hai ngưỡng nhiệt độ

Ảnh cho thấy hai bộ kích hoạt numeric_state, phần điều kiện để trống và khối choose.
Bước 4 – Cấu hình YAML hoàn chỉnh
Nếu không muốn tạo từng khối trong giao diện, có thể mở trình sửa YAML của chính Automation rồi dùng cấu hình dưới đây:
alias: Bai 8.4 - Tu dong bat tat LED
description: Tu dong dieu khien LED theo nhiet do
 
triggers:
  - trigger: numeric_state
    entity_id: input_number.nhiet_do_mo_phong
    above: 30
    id: bat
 
  - trigger: numeric_state
    entity_id: input_number.nhiet_do_mo_phong
    below: 28
    id: tat
 
conditions: []
 
actions:
  - choose:
      - conditions:
          - condition: trigger
            id: bat
        sequence:
          - action: light.turn_on
            target:
              entity_id: light.nguyevietgiang_led_rgb_7_mau
 
      - conditions:
          - condition: trigger
            id: tat
        sequence:
          - action: light.turn_off
            target:
              entity_id: light.nguyevietgiang_led_rgb_7_mau
 
mode: single
Chú ý khi dán: Đoạn YAML này dành cho trình chỉnh sửa YAML của một Automation như trong ảnh, không phải firmware ESPHome và cũng không phải một file Arduino .ino. Nếu đưa vào automations.yaml theo cách thủ công, cấu trúc danh sách ở cấp ngoài có thể cần khác. Không thay entity_id bằng tên hiển thị của thiết bị.
Ảnh 3 – Mã YAML thực tế trong Home Assistant

Các lệnh numeric_state, choose, light.turn_on và light.turn_off xuất hiện trong cấu hình.
Bước 5 – Kích hoạt và kiểm tra
1. Quay về danh sách tự động hóa và bảo đảm Bai 8.4 - Tu dong bat tat LED đang bật.
2. Mở Helper Nhiệt độ mô phỏng.
3. Thực hiện lần lượt các chuyển đổi nhiệt độ trong bảng dưới đây.
4. Quan sát LED thực tế và mục Dấu vết / Traces để xem nhánh bat hoặc tat đã chạy hay chưa.
Ảnh 4 – Automation đã có trong danh sách và được bật

Ảnh cho thấy tự động hóa của bài đã được tạo và công tắc kích hoạt đang bật.
7. Bảng kiểm tra kết quả mong đợi
Để kiểm tra rõ ràng, hãy đặt nhiệt độ ban đầu ở 29°C và tắt LED bằng tay (nếu cần), sau đó thực hiện liên tiếp:
Lần thử	Thay đổi nhiệt độ	Kết quả mong đợi	Lý do
1	29 → 31°C	LED bật	Giá trị vượt ngưỡng 30°C
2	31 → 29°C	LED tiếp tục sáng	Chưa giảm xuống dưới 28°C
3	29 → 27°C	LED tắt	Giá trị xuống dưới 28°C
4	27 → 29°C	LED tiếp tục tắt	Chưa tăng vượt 30°C
5	29 → 30°C	LED vẫn tắt	Bằng 30°C chưa thỏa above: 30
6	30 → 31°C	LED bật	Giá trị lại vượt ngưỡng 30°C

Kết quả kỳ vọng: LED chỉ đổi trạng thái khi giá trị số đi qua đúng ngưỡng. Không nên kết luận rằng code sai chỉ vì để nhiệt độ ở 31°C từ trước rồi nhấn chạy thủ công mà LED không đổi trạng thái.
8. Giải thích các lệnh trong YAML
Thành phần	Ý nghĩa
alias	Tên của Automation
description	Mô tả chức năng
triggers	Danh sách tình huống khởi động tự động hóa
trigger: numeric_state	Theo dõi giá trị số và phát hiện việc đi qua ngưỡng
entity_id	Mã nhận dạng duy nhất của thực thể trong Home Assistant
above: 30	Chỉ kích hoạt khi giá trị chuyển sang lớn hơn 30
below: 28	Chỉ kích hoạt khi giá trị chuyển sang nhỏ hơn 28
id: bat / id: tat	Đặt tên hai trường hợp để khối choose phân biệt
conditions: []	Không có điều kiện chung bổ sung
choose	Chọn nhánh hành động theo bộ kích hoạt đã xảy ra
light.turn_on	Lệnh bật thực thể đèn
light.turn_off	Lệnh tắt thực thể đèn
mode: single	Không tạo lượt chạy chồng lên nhau nếu Automation đang thực thi

9. Video thực hành
▶ XEM VIDEO DEMO: wifi8.mp4 (./wifi8.mp4)
Video minh họa quá trình thao tác với nhiệt độ mô phỏng trên Home Assistant và kiểm tra hệ thống cùng bo mạch ESP32. Nhấp vào liên kết để mở video trong GitHub hoặc tải tệp về xem.
10. Các lỗi thường gặp và cách khắc phục
Hiện tượng	Nguyên nhân cần kiểm tra	Cách xử lý
Thay đổi nhiệt độ nhưng LED không hoạt động	ESP32 hoặc ESPHome chưa kết nối; LED chưa nhận lệnh	Bật/tắt LED thủ công trong Home Assistant trước
LED bật/tắt thủ công được nhưng tự động hóa không chạy	Automation chưa bật hoặc sai ID của Helper	Kiểm tra công tắc Automation, input_number và Dấu vết / Traces
Đang ở 35°C nhưng LED không tự bật sau khi khởi động	numeric_state không tự chạy chỉ vì giá trị đang vượt ngưỡng	Thử đổi xuống 29°C rồi tăng lên 31°C
Chạy nút Run actions / Chạy hành động mà không bật/tắt được LED	Nhánh choose yêu cầu trigger id; chạy hành động thủ công thường không cung cấp ID đó	Kiểm tra bằng cách thay đổi Helper đi qua ngưỡng hoặc mô phỏng một trigger có ID tương ứng
Đặt đúng 30°C hoặc 28°C nhưng không đổi trạng thái	above và below không bao gồm giá trị biên	Thử 31°C và 27°C
YAML báo lỗi	Thụt lề sai, dán nhầm nơi, hoặc tên thực thể không tồn tại	Dùng dấu cách (không dùng tab), kiểm tra ID và dán vào YAML của Automation
Ảnh trong README không hiện	Đường dẫn ảnh hoặc chữ hoa/chữ thường chưa khớp	Đối chiếu chính xác tên file trong thư mục BÀI WIFI

11. Giải đáp nhanh để báo cáo / vấn đáp
Câu 1: Tại sao gọi đây là bài truyền thông WiFi?
ESP32 kết nối với hệ thống qua WiFi để trao đổi dữ liệu và nhận lệnh điều khiển từ Home Assistant/ESPHome. WiFi là công nghệ kết nối không dây trong bài.
Câu 2: Home Assistant làm gì?
Lưu giá trị mô phỏng, theo dõi ngưỡng và quyết định khi nào cần gửi lệnh bật/tắt LED.
Câu 3: ESPHome làm gì?
Giúp cấu hình firmware ESP32, khai báo thực thể LED và kết nối thiết bị với Home Assistant.
Câu 4: numeric_state khác gì với điều kiện numeric_state?
Bộ kích hoạt (trigger) khởi động tự động hóa khi giá trị đi qua ngưỡng; điều kiện (condition) chỉ kiểm tra giá trị tại thời điểm được xét.
Câu 5: Vì sao dùng 30°C để bật và 28°C để tắt?
Để tạo vùng giữ trạng thái giữa hai ngưỡng, tránh việc LED đổi trạng thái liên tục khi giá trị dao động quanh một mốc.
Câu 6: Trong bài có cảm biến nhiệt độ thật không?
Không. Nhiệt độ được nhập bằng Input Number trên Home Assistant. Có thể mở rộng bài bằng cảm biến thật trong một phiên bản khác.
12. Các tệp đi kèm trong thư mục
Tệp	Nội dung
8.4 WIFI.png	Danh sách tự động hóa đã tạo
8.4WIFI.png	Giao diện tạo bộ kích hoạt và hành động
8.4WIFi (2).png	Thực thể LED RGB trong Home Assistant
8.4WWiFI.png	Cấu hình YAML của tự động hóa
wifi8.mp4 (./wifi8.mp4)	Video minh họa thực hành
mã .txt / code.txt	Tệp văn bản được hiển thị với tên khác nhau giữa danh sách chính và cây thư mục trong ảnh; hãy mở tệp thực tế để xem mã
README.md	Tài liệu hướng dẫn này

Lưu ý: Tên các ảnh có dấu cách và phân biệt chữ hoa/chữ thường. Giữ nguyên tên như trên nếu muốn các ảnh hiển thị đúng. Riêng tệp mã: danh sách chính trong ảnh hiển thị mã .txt (có dấu cách trước .txt), còn cây thư mục hiển thị code.txt. Do chưa kiểm tra trực tiếp tệp trên repository nên tài liệu không gắn liên kết mã có thể sai. Hãy bấm tệp mã trong GitHub để kiểm tra tên và nội dung chính xác.
13. Kết luận
Qua bài thực hành, người học đã làm quen với cách kết hợp ESP32 – WiFi – ESPHome – Home Assistant để điều khiển thiết bị IoT theo một quy tắc tự động hóa. Điểm quan trọng là hiểu cách dùng hai ngưỡng 30°C và 28°C, cách numeric_state kích hoạt khi đi qua ngưỡng, và cách choose chọn đúng hành động bật hoặc tắt LED.
Có thể phát triển thành hệ thống thực tế bằng cách thay nhiệt độ mô phỏng bằng giá trị từ cảm biến, đồng thời bổ sung chức năng điều khiển quạt, cảnh báo và hiển thị trạng thái. Khi sử dụng thiết bị điện công suất lớn cần có phần điều khiển, cách ly và bảo vệ phù hợp.
Tài liệu tham khảo
– Home Assistant – Numeric state crossed threshold (https://www.home-assistant.io/triggers/numeric_state/)
– Home Assistant – Choose action (https://www.home-assistant.io/building-blocks/choose/)
– Home Assistant – Troubleshooting automations (https://www.home-assistant.io/docs/automation/troubleshooting/)
– GitHub Docs – About README files (https://docs.github.com/en/repositories/managing-your-repositorys-settings-and-features/customizing-your-repository/about-readmes)
