# Báo cáo đối chiếu STM32 UART ↔ TSMaster

## Kết luận: PASS WITH WARNING

- UART evidence không đầy đủ: gap 19 dòng, TSMaster có 19 frame UNLOGGED

## Tổng quan

| Chỉ số | Kết quả | Ý nghĩa |
|---|---:|---|
| Clock locked | `true` | Đã tìm được offset giữa clock STM32 và TSMaster |
| Offset | -16344.126 ms | Chênh lệch gốc clock, **không phải** CAN latency tuyệt đối |
| TX logged / queued | 1480 / 1480 | Frame firmware ghi log / queue thành công |
| TX busy / error | 0 / 0 | Lỗi trước khi frame vào CAN mailbox |
| UART log gaps | 19 | Dòng `$TX` thiếu trong UART evidence |
| TSMaster RX | 1499 | Tổng frame đọc từ ASC/JSONL |
| MATCH | 1480 | ID + 8 byte giống nhau trong cửa sổ thời gian |
| LOST | 0 | Có TX nhưng không tìm thấy RX |
| MISMATCH | 0 | Đúng frame time/ID nhưng khác payload |
| UNEXPECTED | 0 | Có RX nhưng không có TX tương ứng |
| UNLOGGED | 19 | TSMaster nhận frame trong đoạn UART bị gap |
| Match rate | 100.0% | `MATCH / (MATCH + LOST + MISMATCH)` |

## Timing

`latency_jitter_ms` là residual sau khi loại offset và bám clock drift; không được diễn giải là độ trễ vật lý tuyệt đối giữa STM32 và TSMaster.

- Samples: 1480
- Residual mean/std: -0.111 / 0.103 ms
- Residual min/max: -0.24 / 0.665 ms
- Estimated clock drift: -136.7 ppm

### 0x0CF00400 — PGN 61444

- TX log period: mean 20.257 ms, min 20.0, max 40.0.
- RX bus period: mean 19.997 ms, min 19.914, max 20.065, std 0.024.
- Khi UART có gap, RX period là bằng chứng đáng tin cậy hơn cho timing trên bus.

## Kiểm tra SPN

| SPN | Tên | Checked | Value OK | Fail | Giá trị cuối |
|---:|---|---:|---:|---:|---|
| 190 | Engine Speed | 1480 | 1480 | 0 | 2967.875 rpm |

## Nguồn bằng chứng

| File | SHA-256 |
|---|---|
| `correlation_runs\sae\stm32_uart.log` | `1bdcdff06b287d0fb8c4ad98107eee60dda6e4b7ec78384c800e5853022d543f` |
| `correlation_runs\sae\tsmaster.asc` | `c7715e5dba3e17a8583b63d94cc4576c8c974d07f26de7126acd02b478804edc` |
| `correlation_runs\spn190_sine_config.json` | `4caa3ee1028e4167fed7ac620230d6a1e55774ad845a5e728ac013f7ee6aa45b` |

## Quy tắc kết luận

- **FAIL:** không lock clock; có BUSY/ERROR, LOST, MISMATCH, UNEXPECTED hoặc SPN fail.
- **PASS WITH WARNING:** dữ liệu đã match nhưng UART evidence có gap/UNLOGGED hoặc có foreign frames.
- **PASS:** clock lock, dữ liệu/SPN đúng và evidence không có gap.
