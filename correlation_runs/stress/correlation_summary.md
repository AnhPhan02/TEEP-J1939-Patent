# Báo cáo đối chiếu STM32 UART ↔ TSMaster

## Kết luận: PASS WITH WARNING

- UART evidence không đầy đủ: gap 40 dòng, TSMaster có 40 frame UNLOGGED

## Tổng quan

| Chỉ số | Kết quả | Ý nghĩa |
|---|---:|---|
| Clock locked | `true` | Đã tìm được offset giữa clock STM32 và TSMaster |
| Offset | -16349.511 ms | Chênh lệch gốc clock, **không phải** CAN latency tuyệt đối |
| TX logged / queued | 5959 / 5959 | Frame firmware ghi log / queue thành công |
| TX busy / error | 0 / 0 | Lỗi trước khi frame vào CAN mailbox |
| UART log gaps | 40 | Dòng `$TX` thiếu trong UART evidence |
| TSMaster RX | 5999 | Tổng frame đọc từ ASC/JSONL |
| MATCH | 5959 | ID + 8 byte giống nhau trong cửa sổ thời gian |
| LOST | 0 | Có TX nhưng không tìm thấy RX |
| MISMATCH | 0 | Đúng frame time/ID nhưng khác payload |
| UNEXPECTED | 0 | Có RX nhưng không có TX tương ứng |
| UNLOGGED | 40 | TSMaster nhận frame trong đoạn UART bị gap |
| Match rate | 100.0% | `MATCH / (MATCH + LOST + MISMATCH)` |

## Timing

`latency_jitter_ms` là residual sau khi loại offset và bám clock drift; không được diễn giải là độ trễ vật lý tuyệt đối giữa STM32 và TSMaster.

- Samples: 5959
- Residual mean/std: -0.029 / 0.047 ms
- Residual min/max: -0.118 / 0.649 ms
- Estimated clock drift: -137.9 ppm

### 0x0CF00400 — PGN 61444

- TX log period: mean 5.034 ms, min 5.0, max 10.0.
- RX bus period: mean 4.999 ms, min 4.907, max 5.072, std 0.025.
- Khi UART có gap, RX period là bằng chứng đáng tin cậy hơn cho timing trên bus.

## Kiểm tra SPN

| SPN | Tên | Checked | Value OK | Fail | Giá trị cuối |
|---:|---|---:|---:|---:|---|
| 190 | Engine Speed | 5959 | 5959 | 0 | 2982.625 rpm |

## Nguồn bằng chứng

| File | SHA-256 |
|---|---|
| `correlation_runs\stress\stm32_uart.log` | `286e4a03fa557958e7595e06f367a89c932707c97848448cecd07e995911add4` |
| `correlation_runs\stress\tsmaster.asc` | `8a1f0e45528191008a5a6686302bf204a825b59c45d20195bc5499a9a16e391b` |
| `correlation_runs\spn190_sine_config.json` | `4caa3ee1028e4167fed7ac620230d6a1e55774ad845a5e728ac013f7ee6aa45b` |

## Quy tắc kết luận

- **FAIL:** không lock clock; có BUSY/ERROR, LOST, MISMATCH, UNEXPECTED hoặc SPN fail.
- **PASS WITH WARNING:** dữ liệu đã match nhưng UART evidence có gap/UNLOGGED hoặc có foreign frames.
- **PASS:** clock lock, dữ liệu/SPN đúng và evidence không có gap.
