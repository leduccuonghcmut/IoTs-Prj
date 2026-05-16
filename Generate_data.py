"""
generate_data.py  —  v3 final
Fix hoàn toàn overlap threshold vs spike.

Core insight:
  - Threshold là "trạng thái bền vững" → giá trị cao/thấp liên tục, delta NHỎ
  - Spike là "cú nhảy đột ngột" → delta LỚN nhưng temp trở về bình thường
  → Hai class phải tách biệt hoàn toàn trên không gian (temp, delta_temp)

Fix:
  1. Threshold block: reset prev_temp VÀO ĐÚNG VÙNG threshold ngay từ đầu
     → delta đầu block nhỏ (drift trong vùng threshold, không nhảy từ normal lên)
  2. Spike: giữ clip 15–39°C và humidity 21–95% như v3
  3. Thêm delta_humidity vào sanity check
"""

import numpy as np
import pandas as pd
import random

random.seed(42)
np.random.seed(42)

TOTAL           = 1200
NORMAL_RATIO    = 0.70
THRESHOLD_RATIO = 0.15
SPIKE_RATIO     = 0.15

TEMP_HIGH            = 40.0
HUMIDITY_LOW         = 20.0
DELTA_TEMP_SPIKE     = 8.0
DELTA_HUMIDITY_SPIKE = 20.0
ROLLING_WINDOW       = 5

n_normal    = int(TOTAL * NORMAL_RATIO)
n_threshold = int(TOTAL * THRESHOLD_RATIO)
n_spike     = TOTAL - n_normal - n_threshold

block_list = []
for seg_type, seg_count in [("normal", n_normal),
                             ("threshold", n_threshold),
                             ("spike", n_spike)]:
    block_size = 10
    for start in range(0, seg_count, block_size):
        count = min(block_size, seg_count - start)
        block_list.append((seg_type, count))

random.shuffle(block_list)

rows = []
current_time = pd.Timestamp("2024-01-01 08:00:00")
INTERVAL_SEC = 5

prev_temp     = 28.0
prev_humidity = 60.0
prev_type     = None
prev_choice   = None  # lưu loại threshold để reset đúng vùng

for seg_type, count in block_list:
    # Reset prev vào đúng vùng của block mới
    if seg_type != prev_type:
        if seg_type == "normal":
            prev_temp     = np.random.uniform(26.0, 30.0)
            prev_humidity = np.random.uniform(55.0, 65.0)
        elif seg_type == "threshold":
            # Chọn loại threshold trước, rồi reset prev VÀO VÙNG ĐÓ
            prev_choice = random.choice(["high_temp", "low_humidity", "both"])
            if prev_choice == "high_temp":
                prev_temp     = np.random.uniform(TEMP_HIGH + 1, 55.0)
                prev_humidity = np.random.uniform(30.0, 65.0)
            elif prev_choice == "low_humidity":
                prev_temp     = np.random.uniform(24.0, 36.0)
                prev_humidity = np.random.uniform(6.0, HUMIDITY_LOW - 1)
            else:
                prev_temp     = np.random.uniform(TEMP_HIGH + 1, 55.0)
                prev_humidity = np.random.uniform(6.0, HUMIDITY_LOW - 1)
        else:  # spike — reset về normal để delta spike tính từ nền bình thường
            prev_temp     = np.random.uniform(25.0, 31.0)
            prev_humidity = np.random.uniform(50.0, 68.0)
    prev_type = seg_type

    for _ in range(count):
        if seg_type == "normal":
            temp     = np.clip(prev_temp     + np.random.normal(0, 0.5), 22.0, 38.0)
            humidity = np.clip(prev_humidity + np.random.normal(0, 1.0), 40.0, 75.0)
            label = 0

        elif seg_type == "threshold":
            # Drift nhỏ trong vùng threshold (prev đã ở trong vùng đó)
            if prev_choice == "high_temp":
                temp     = np.clip(prev_temp     + np.random.normal(0, 1.0), TEMP_HIGH + 0.5, 60.0)
                humidity = np.clip(prev_humidity + np.random.normal(0, 1.5), 22.0, 72.0)
            elif prev_choice == "low_humidity":
                temp     = np.clip(prev_temp     + np.random.normal(0, 0.5), 22.0, 38.0)
                humidity = np.clip(prev_humidity + np.random.normal(0, 0.8), 5.0, HUMIDITY_LOW - 0.5)
            else:
                temp     = np.clip(prev_temp     + np.random.normal(0, 1.0), TEMP_HIGH + 0.5, 60.0)
                humidity = np.clip(prev_humidity + np.random.normal(0, 0.8), 5.0, HUMIDITY_LOW - 0.5)
            label = 1

        else:  # spike
            direction  = random.choice([-1, 1])
            raw_temp   = prev_temp + direction * np.random.uniform(8.5, 14.0)
            temp       = np.clip(raw_temp, 15.0, 39.0)

            direction2   = random.choice([-1, 1])
            raw_humidity = prev_humidity + direction2 * np.random.uniform(20.5, 35.0)
            humidity     = np.clip(raw_humidity, 21.0, 95.0)
            label = 2

        delta_temp     = round(temp     - prev_temp,     2)
        delta_humidity = round(humidity - prev_humidity, 2)

        rows.append({
            "timestamp"     : current_time.strftime("%Y-%m-%d %H:%M:%S"),
            "temp"          : round(temp, 2),
            "humidity"      : round(humidity, 2),
            "delta_temp"    : delta_temp,
            "delta_humidity": delta_humidity,
            "label"         : label
        })

        prev_temp     = temp
        prev_humidity = humidity
        current_time += pd.Timedelta(seconds=INTERVAL_SEC)

df = pd.DataFrame(rows)

df["rolling_mean_temp"] = (
    df["temp"].rolling(window=ROLLING_WINDOW, min_periods=1).mean().round(2)
)
df["rolling_std_temp"] = (
    df["temp"].rolling(window=ROLLING_WINDOW, min_periods=1).std().fillna(0).round(2)
)

df.to_csv("sensor_data.csv", index=False)

th = df[df.label == 1]
sp = df[df.label == 2]
nm = df[df.label == 0]

print("=== Sanity check ===")
print(f"  Threshold |delta_temp| > 8  : {(th.delta_temp.abs()>8).sum()}/{len(th)} ({(th.delta_temp.abs()>8).mean()*100:.1f}%)  [mục tiêu < 5%]")
print(f"  Spike temp > 40             : {(sp.temp>40).sum()}/{len(sp)} ({(sp.temp>40).mean()*100:.1f}%)  [mục tiêu = 0%]")
print(f"  Spike humidity < 20         : {(sp.humidity<20).sum()}/{len(sp)} ({(sp.humidity<20).mean()*100:.1f}%)  [mục tiêu = 0%]")
print(f"  Spike |delta_temp| <= 8     : {(sp.delta_temp.abs()<=8).sum()}/{len(sp)} ({(sp.delta_temp.abs()<=8).mean()*100:.1f}%)  [mục tiêu < 5%]")

print("\n=== Thống kê dataset ===")
print(f"Tổng số mẫu    : {len(df)}")
print(f"Normal    (0)  : {(df.label==0).sum()} ({(df.label==0).mean()*100:.1f}%)")
print(f"Threshold (1)  : {(df.label==1).sum()} ({(df.label==1).mean()*100:.1f}%)")
print(f"Spike     (2)  : {(df.label==2).sum()} ({(df.label==2).mean()*100:.1f}%)")

print(f"\n[normal]    temp: {nm.temp.min():.1f}–{nm.temp.max():.1f}  |delta_temp| mean: {nm.delta_temp.abs().mean():.2f}")
print(f"[threshold] temp: {th.temp.min():.1f}–{th.temp.max():.1f}  |delta_temp| mean: {th.delta_temp.abs().mean():.2f}")
print(f"[spike]     temp: {sp.temp.min():.1f}–{sp.temp.max():.1f}  |delta_temp| mean: {sp.delta_temp.abs().mean():.2f}")

print(f"\nRolling std mean:")
print(f"  normal    : {nm.rolling_std_temp.mean():.3f}")
print(f"  threshold : {th.rolling_std_temp.mean():.3f}")
print(f"  spike     : {sp.rolling_std_temp.mean():.3f}")

print("\nFile đã lưu: sensor_data.csv")