# Báo cáo đối chiếu STM32 UART ↔ TSMaster

## Kết luận: PASS WITH WARNING

- UART evidence không đầy đủ: gap 29 dòng, TSMaster có 29 frame UNLOGGED

## Tổng quan

| Chỉ số | Kết quả | Ý nghĩa |
|---|---:|---|
| Clock locked | `true` | Đã tìm được offset giữa clock STM32 và TSMaster |
| Offset | -16338.283 ms | Chênh lệch gốc clock, **không phải** CAN latency tuyệt đối |
| TX logged / queued | 2970 / 2970 | Frame firmware ghi log / queue thành công |
| TX busy / error | 0 / 0 | Lỗi trước khi frame vào CAN mailbox |
| UART log gaps | 29 | Dòng `$TX` thiếu trong UART evidence |
| TSMaster RX | 2999 | Tổng frame đọc từ ASC/JSONL |
| MATCH | 2970 | ID + 8 byte giống nhau trong cửa sổ thời gian |
| LOST | 0 | Có TX nhưng không tìm thấy RX |
| MISMATCH | 0 | Đúng frame time/ID nhưng khác payload |
| UNEXPECTED | 0 | Có RX nhưng không có TX tương ứng |
| UNLOGGED | 29 | TSMaster nhận frame trong đoạn UART bị gap |
| Match rate | 100.0% | `MATCH / (MATCH + LOST + MISMATCH)` |

## Timing

`latency_jitter_ms` là residual sau khi loại offset và bám clock drift; không được diễn giải là độ trễ vật lý tuyệt đối giữa STM32 và TSMaster.

- Samples: 2970
- Residual mean/std: -0.063 / 0.07 ms
- Residual min/max: -0.173 / 0.664 ms
- Estimated clock drift: -152.5 ppm

### 0x0CF00400 — PGN 61444

- TX log period: mean 10.098 ms, min 10.0, max 30.0.
- RX bus period: mean 9.999 ms, min 9.887, max 10.067, std 0.022.
- Khi UART có gap, RX period là bằng chứng đáng tin cậy hơn cho timing trên bus.

## Kiểm tra SPN

| SPN | Tên | Checked | Value OK | Fail | Giá trị cuối |
|---:|---|---:|---:|---:|---|
| 190 | Engine Speed | 2970 | 2970 | 0 | 2977.625 rpm |

## Nguồn bằng chứng

| File | SHA-256 |
|---|---|
| `correlation_runs\smooth\stm32_uart.log` | `1e5ba7a672bb79aa4ee1b4ddfedd8184f1eacddc3b91e5a10f47f91c810da4ca` |
| `correlation_runs\smooth\tsmaster.asc` | `22aeca7ae6e5bdcaf3b46c745c3bfa0eac85b287c6bc90e7b9c4f0f917d7a5f9` |
| `correlation_runs\spn190_sine_config.json` | `4caa3ee1028e4167fed7ac620230d6a1e55774ad845a5e728ac013f7ee6aa45b` |

## Quy tắc kết luận

- **FAIL:** không lock clock; có BUSY/ERROR, LOST, MISMATCH, UNEXPECTED hoặc SPN fail.
- **PASS WITH WARNING:** dữ liệu đã match nhưng UART evidence có gap/UNLOGGED hoặc có foreign frames.
- **PASS:** clock lock, dữ liệu/SPN đúng và evidence không có gap.
