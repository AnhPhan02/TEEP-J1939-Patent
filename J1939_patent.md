# J1939 STM32 Signal Generator & Verification Platform — Tổng hợp tìm hiểu

> Tài liệu này đúc kết quá trình đọc mã nguồn trong repo `TEEP-Intern-overall-code`. Mục đích: ghi lại overview, luồng vận hành, và đánh giá hướng phát triển của hệ thống sinh & kiểm chứng dữ liệu J1939 trên STM32F103 truyền qua CAN tới TSMaster.

## 1. Overview

Đây là một **test-bench mô phỏng J1939** dùng STM32F103 làm nguồn traffic CAN "known-good", phục vụ mục tiêu chính là **kiểm chứng một sản phẩm đầu dò CAN không tiếp xúc** (theo các báo cáo `SWISYS_Contactless_CAN_Probe_*_Validation_Report*.docx` ở root). Hệ thống gồm 5 lớp:

| Lớp | File chính | Vai trò |
|---|---|---|
| Firmware sinh tín hiệu | `j1939_arduino_generator.ino`, `j1939_pattern_generator.c/h` | Sinh 52 SPN / 17 PGN theo waveform (sine, ramp, step, random walk...) |
| Mã hoá & truyền CAN | `j1939_encode_decode.c/h`, `j1939_can_helper.cpp/h` | Đóng gói bit theo J1939, dựng CAN-ID 29 bit, đẩy vào bxCAN của STM32 |
| Cơ sở dữ liệu tín hiệu | `j1939_signal_definitions.c/h` | "Nguồn sự thật" duy nhất cho mọi PGN/SPN (vị trí byte/bit, resolution, offset, range vật lý) |
| Điều khiển & DBC | `web_server.py` + `web/` (FastAPI + HTML/JS) + `dbc_generator.py` | Dashboard web điều khiển STM32 qua Serial, sinh file `.dbc` động |
| Kiểm chứng | `realtime_j1939_verifier.py`, `tsmaster_mini_program_bridge.py`, các script `compare_*.py` / `analyze_*.py` | Đối chiếu khung CAN nhận được (qua TSMaster) với DBC/pattern kỳ vọng |

`TSMASTER_DATABASE_README.md` chỉ là hướng dẫn nạp file `.dbc` vào TSMaster để giải mã — phần lõi kỹ thuật nằm ở firmware và bộ sinh DBC.

Tác giả ghi trong code: Vishal Meyyappan R (ECE, CIT Chennai) & Srikar — dự án hợp tác CIT Chennai & STUST.

## 2. Kiến trúc & luồng vận hành

### 2.1 Input

- **Định nghĩa tín hiệu cứng** trong firmware: 52 SPN thuộc 17 PGN (ví dụ SPN 190 Engine Speed trong PGN EEC1 61444), mỗi SPN có `start_byte`, `start_bit`, `num_bits`, `resolution`, `offset`, `min_physical`, `max_physical` — khai báo trong `j1939_signal_definitions.c`.
- **Cấu hình pattern** do người dùng nhập, qua Web UI hoặc lệnh serial thô:
  `CONFIG <spn> <pattern_type> <min> <max> <param1> [timeframe_ms] [t_start] [t_dur]`
  - `pattern_type`: 0=Constant, 1=Ramp (sawtooth), 2=Sine, 3=Triangle, 4=Step, 5=Square, 6=Random Walk, 7=State Sequence.
- **Kết nối phần cứng**: STM32 Nucleo (CAN1 remap PB8/PB9) → MCP2551 transceiver → CAN_H/CAN_L → TSMaster, hai đầu bus có điện trở kết cuối 120Ω.

### 2.2 Process

1. **Boot**: firmware tự nạp 3 tín hiệu mặc định (Engine Speed sine 800–3500rpm, Accel Pedal ramp 0–100%, Vehicle Speed ramp 0–120km/h) và tự `START` ở mode 0 (liên tục, SAE-compliant) — xem `Load_Default_Signals()` và `setup()` trong `j1939_arduino_generator.ino`.
2. **Điều khiển runtime**: người dùng gửi lệnh qua dashboard web (`web_server.py` ghi trực tiếp vào cổng Serial 115200 baud) hoặc Serial Monitor: `CONFIG`, `START <duration_sec> <mode>`, `STOP`, `CLEAR`, `BAUD <250|500>`, `STATUS`, `RESET`.
   - Mode 0 = High-Fidelity Smooth Waveform (chu kỳ PGN 10–25ms, 50Hz update để đồ thị mượt trên TSMaster).
   - Mode 1 = SAE Standard timing (20ms–1000ms theo chuẩn J1939 thật).
   - Mode 2 = Stress Test (5ms–100ms, tải bus tối đa).
3. **Sinh giá trị**: mỗi 1ms, `Pattern_Generator_Update()` tính lại giá trị tức thời cho tất cả pattern đang active (công thức toán học riêng cho từng loại: sawtooth, sin, triangle, step, xorshift32 PRNG cho random walk...). Có "startup lead-in" 5 giây trả về 0.0 trước khi pattern bắt đầu chạy thật.
4. **Đóng gói & mã hoá**: khi tới chu kỳ phát của một PGN, firmware gộp mọi SPN thuộc PGN đó vào payload 8 byte (khởi tạo `0xFF` = "not available" theo chuẩn J1939), gọi `J1939_Encode_Signal()`:
   - `raw = round((physical - offset) / resolution)`, clamp theo `min_physical`/`max_physical` và theo số bit khả dụng.
   - Có "fast path" ghi trực tiếp cho tín hiệu byte-aligned (8/16/24/32 bit) và "slow path" ghi bit-by-bit cho tín hiệu lệch byte hoặc <8 bit.
5. **Truyền CAN**: `J1939_CAN_Hardware_Transmit()` dựng CAN-ID mở rộng 29-bit theo đúng cấu trúc J1939 (Priority 3 bit | Data Page 1 bit | PDU Format 8 bit | PDU Specific/Destination 8 bit | Source Address 8 bit), rồi đẩy vào Tx mailbox của bxCAN (STM32 HAL). Có cơ chế timeout 2ms + log chẩn đoán lỗi bus (ESR, TEC/REC, Last Error Code) khi mailbox đầy.
6. **Nhận & giải mã trên TSMaster**: import `STM32F103_J1939_Signal_Generator.dbc`, cấu hình kênh Classic CAN, chọn đúng bitrate, xem Trace/Graphics để thấy các ID quen thuộc (vd `0CF00400` = EEC1).
7. **Kiểm chứng độc lập (song song)**: `realtime_j1939_verifier.py` (dashboard Tkinter) đọc cùng file DBC, nhận khung theo 1 trong 2 cách:
   - Trực tiếp qua `python-can` (nếu sở hữu adapter CAN vật lý), hoặc
   - Qua cầu nối TCP: một Python Mini Program chạy **bên trong TSMaster** dùng `tsmaster_mini_program_bridge.py` để forward mỗi khung nhận được dưới dạng JSON qua socket cục bộ `127.0.0.1:29500`.
   Verifier kiểm tra: ID 29-bit đúng, DLC đúng, giải mã byte-level đúng, giá trị nằm trong range kỹ thuật, đúng chu kỳ phát, và (nếu có) đúng giá trị kỳ vọng từ `verification_test_profile.json` (test giá trị cố định, ví dụ Engine Speed=1500rpm, Vehicle Speed=60km/h, Brake Pedal=30%).
8. **Hậu kiểm offline**: các log CSV/`.trc` chụp từ TSMaster trong nhiều kịch bản test (bình thường, stress 1ms–12ms...) được đối chiếu với pattern kỳ vọng bằng các script `compare_logged_patterns.py`, `analyze_52_spns.py`, `generate_real_52_spn_audit.py`..., xuất ra hàng loạt `comparison_results_*.xlsx` — đây là dữ liệu nền cho các báo cáo validation `.docx` của đầu dò CAN.

### 2.3 Config cần lưu ý / thay đổi

- **⚠️ Baud rate CAN — có mâu thuẫn giữa README và code mặc định**:
  - `TSMASTER_DATABASE_README.md` khẳng định firmware cấu hình **250 kbit/s**.
  - Nhưng biến khởi tạo trong `j1939_arduino_generator.ino` là `g_current_baud_kbps = 500` (dòng 61), và thông báo lỗi trong `j1939_can_helper.cpp` cũng hỏi "Is TSMaster... set to 500 kbps?".
  - **Cần verify thực tế trên board** trước khi cấu hình channel TSMaster — nếu lệch bitrate, Trace sẽ hiện raw frame nhưng không giải mã được (đúng hiện tượng README đã cảnh báo).
- Có 2 file `.dbc` gần như giống hệt ở root: `STM32F103_J1939_Signal_Generator.dbc` và `verfication.dbc` (cùng dung lượng 26500 bytes) — không rõ file nào là canonical, dễ gây nhầm khi import vào TSMaster.
- `web_server.py` cho phép sinh DBC động (`/api/dbc/generate`, `/api/dbc/download`) theo đúng tập tín hiệu đang active + mode + baud — nên đây mới là nguồn DBC "tươi" nhất khi cấu hình runtime thay đổi, khác với file tĩnh ở root.

### 2.4 Output

- Khung CAN thực trên bus, được TSMaster giải mã trực quan qua Trace/Graphics.
- Kết quả PASS/FAIL theo thời gian thực từ `realtime_j1939_verifier.py` (đỏ = sai ID/DLC/scale/offset/source-address/thiếu khung/ngoài range).
- File DBC xuất động (`.dbc`) khớp chính xác tập tín hiệu đang chạy.
- Báo cáo so sánh Excel/CSV (`comparison_results_*.xlsx`) và tài liệu Word validation cho đầu dò CAN không tiếp xúc (SWISYS).

## 3. Đánh giá hướng phát triển

### Điểm mạnh

- Kiến trúc tách lớp rõ ràng: encode/decode ↔ pattern generator ↔ CAN HAL ↔ web control ↔ verifier — mỗi module một trách nhiệm, dễ đọc.
- Một nguồn sự thật duy nhất cho signal layout (`j1939_signal_definitions.c`), từ đó sinh JSON và DBC.
- Encode/decode có cả fast-path (byte-aligned) và slow-path (bit-by-bit) — tối ưu hợp lý cho vi điều khiển mà vẫn tổng quát cho mọi layout.
- Tính toán bit-timing CAN tự động theo nhiều tần số PCLK1 khác nhau (không hard-code) — khá chuyên nghiệp cho firmware bare-metal HAL.
- Có pipeline kiểm chứng vòng kín: generator → DBC → TSMaster → verifier → báo cáo, đúng tinh thần validation sản phẩm thật.

### Rủi ro / điểm cần cải thiện

1. **Mâu thuẫn baud rate 250 vs 500 kbit/s** giữa README và default code — cần thống nhất và ghi rõ trong tài liệu, tránh gây lỗi khi người mới setup TSMaster.
2. **Repo phình to vì dữ liệu log/kết quả**: hàng chục file `.CSV`/`.xlsx`/`.trc` nặng (nhiều file >5–10MB) commit thẳng vào git (`stress_test_logged_trc.csv` ~10MB, `TSMaster2026_07_29_10_07_08.CSV` ~7.5MB...). Nên tách sang Git LFS hoặc loại khỏi source control, chỉ giữ code + báo cáo cuối.
3. **File build artifact bị commit** (`j1939_encode_decode.o`, `j1939_pattern_generator.o`, `j1939_signal_definitions.o`) — nên thêm `.gitignore`.
4. **`requirements.txt` không đầy đủ**: chỉ liệt kê `python-can`, trong khi `web_server.py` cần thêm `fastapi`, `uvicorn`, `pyserial`, `pydantic` — người mới clone sẽ gặp lỗi thiếu dependency ngay khi chạy web dashboard.
5. **Chuỗi sinh dữ liệu DBC dựa trên regex parse file `.c`**: `generate_database.py` dùng regex đọc ngược `j1939_signal_definitions.c` → JSON → rồi `dbc_generator.py` build DBC từ JSON. Cách này hoạt động nhưng dễ vỡ khi format code thay đổi nhẹ (thêm comment, đổi thứ tự field...). Hướng cải thiện: định nghĩa tín hiệu một lần ở dạng cấu trúc (YAML/JSON) làm nguồn duy nhất, rồi generate cả C header/array lẫn DBC từ đó — loại bỏ vòng "đọc ngược" mã nguồn.
6. **Thiếu CI/automation**: `verify_database_dbc.py` (kiểm tra bit layout không vượt 64-bit, resolution hợp lệ, min<max, parse thử bằng `cantools`) là script rất hữu ích nhưng chỉ chạy tay. Nên đưa vào CI để tự động chạy mỗi khi `j1939_signal_definitions.c`/`j1939_spn_database.json` thay đổi, tránh drift giữa firmware thật và DBC dùng để verify.
7. **Chưa có unit test host-side cho encode/decode**: hiện correctness chỉ được xác nhận gián tiếp qua so sánh log TSMaster sau khi đã chạy trên phần cứng thật — phát hiện lỗi khá muộn. Có thể build lại `j1939_encode_decode.c` trên máy tính (không phụ thuộc HAL) và viết test round-trip physical→raw→physical cho cả 52 SPN, chạy trước khi nạp firmware.
8. **Hai file DBC trùng lặp ở root** (`STM32F103_J1939_Signal_Generator.dbc` vs `verfication.dbc`, có cả lỗi chính tả "verfication") — nên gộp lại một nguồn canonical, hoặc làm rõ mục đích khác nhau của từng file nếu có.

### Đề xuất ưu tiên

1. Xác minh baud rate thật trên board, sửa lại README hoặc code cho khớp — vì đây là lỗi setup phổ biến nhất được chính README cảnh báo.
2. Dọn dẹp repo: chuyển log/kết quả (`*.CSV`, `*.xlsx`, `*.trc`) ra khỏi git hoặc dùng Git LFS; xoá `*.o` đã build.
3. Hoàn thiện `requirements.txt` cho đủ cả phần web server.
4. Đưa `verify_database_dbc.py` vào một bước kiểm tra tự động (CI hoặc pre-commit hook) thay vì chạy tay.
