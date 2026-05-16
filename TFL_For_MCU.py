# """
# train_model.py  —  v2
# Pipeline training TinyML cho DHT20 Anomaly Detection.

# Cải thiện so với v1:
#   ① Data:
#      - 6 features thay vì 4 (thêm rolling_mean_temp, rolling_std_temp)
#      - Label đúng từ generate_data v2 (không bị relabel sai)

#   ② Model:
#      - Kiến trúc sâu hơn: 32→16→8→3
#      - BatchNormalization sau mỗi Dense layer → ổn định training
#      - Dropout(0.3) → giảm overfitting (thấy ở v1: train loss << val loss)

#   ③ Training:
#      - class_weight tự động → cân bằng threshold/spike bị underrepresented
#      - ReduceLROnPlateau → giảm LR khi val_loss plateau thay vì dừng sớm
#      - EarlyStopping patience=20 (v1 dùng 10, cắt quá sớm)
#      - epochs=200 để model hội tụ đủ

#   ④ Export:
#      - Vẫn giữ full pipeline: .h5, .tflite, .h, scaler.json
#      - Header C++ cập nhật đủ 6 scaler constants
# """

# import numpy as np
# import pandas as pd
# import matplotlib
# matplotlib.use("Agg")
# import matplotlib.pyplot as plt
# import json
# from sklearn.model_selection import train_test_split
# from sklearn.preprocessing import MinMaxScaler
# from sklearn.metrics import (confusion_matrix, classification_report,
#                              ConfusionMatrixDisplay)
# from sklearn.utils.class_weight import compute_class_weight
# import tensorflow as tf

# PREFIX = "TinyML_v2"

# # ── 1. Load data ──────────────────────────────────────────────────────────────
# print("=" * 55)
# print("1. LOADING DATA")
# print("=" * 55)

# df = pd.read_csv("sensor_data.csv")
# print(f"Tổng mẫu: {len(df)}")
# print(df["label"].value_counts().rename({0: "normal", 1: "threshold", 2: "spike"}))
# print()

# # 6 features — thêm rolling để phân biệt threshold (bền vững) vs spike (đột ngột)
# FEATURES = [
#     "temp", "humidity",
#     "delta_temp", "delta_humidity",
#     "rolling_mean_temp", "rolling_std_temp"
# ]
# X = df[FEATURES].values
# y = df["label"].values

# # ── 2. Normalize ──────────────────────────────────────────────────────────────
# print("=" * 55)
# print("2. NORMALIZING")
# print("=" * 55)

# scaler   = MinMaxScaler()
# X_scaled = scaler.fit_transform(X)

# scaler_params = {
#     "features": FEATURES,
#     "min"     : scaler.data_min_.tolist(),
#     "max"     : scaler.data_max_.tolist(),
#     "scale"   : scaler.scale_.tolist()
# }
# with open(PREFIX + "_scaler.json", "w") as f:
#     json.dump(scaler_params, f, indent=2)

# print("Scaler params:")
# for i, feat in enumerate(FEATURES):
#     print(f"  {feat:25s}: min={scaler.data_min_[i]:.3f}, max={scaler.data_max_[i]:.3f}")
# print()

# # ── 3. Train/test split ───────────────────────────────────────────────────────
# X_train, X_test, y_train, y_test = train_test_split(
#     X_scaled, y, test_size=0.2, random_state=42, stratify=y
# )
# print(f"Train: {len(X_train)} | Test: {len(X_test)}")
# print()

# # ── 4. Class weights (fix imbalance) ─────────────────────────────────────────
# classes      = np.unique(y_train)
# weights_arr  = compute_class_weight("balanced", classes=classes, y=y_train)
# class_weight = dict(zip(classes, weights_arr))
# print(f"Class weights: {class_weight}")
# print()

# # ── 5. Build model ────────────────────────────────────────────────────────────
# print("=" * 55)
# print("3. BUILDING MODEL")
# print("=" * 55)

# NUM_CLASSES = 3

# model = tf.keras.Sequential([
#     tf.keras.layers.Input(shape=(len(FEATURES),)),

#     # Block 1
#     tf.keras.layers.Dense(32),
#     tf.keras.layers.BatchNormalization(),
#     tf.keras.layers.Activation("relu"),
#     tf.keras.layers.Dropout(0.3),

#     # Block 2
#     tf.keras.layers.Dense(16),
#     tf.keras.layers.BatchNormalization(),
#     tf.keras.layers.Activation("relu"),
#     tf.keras.layers.Dropout(0.2),

#     # Block 3
#     tf.keras.layers.Dense(8, activation="relu"),

#     # Output
#     tf.keras.layers.Dense(NUM_CLASSES, activation="softmax")
# ], name="dht20_anomaly_v2")

# model.compile(
#     loss="sparse_categorical_crossentropy",
#     optimizer=tf.keras.optimizers.Adam(learning_rate=1e-3),
#     metrics=["accuracy"]
# )
# model.summary()
# print()

# # ── 6. Train ──────────────────────────────────────────────────────────────────
# print("=" * 55)
# print("4. TRAINING")
# print("=" * 55)

# callbacks = [
#     # Dừng nếu val_loss không cải thiện sau 20 epoch (v1 dùng 10 — cắt quá sớm)
#     tf.keras.callbacks.EarlyStopping(
#         monitor="val_loss", patience=20, restore_best_weights=True, verbose=1
#     ),
#     # Giảm LR khi plateau thay vì dừng ngay
#     tf.keras.callbacks.ReduceLROnPlateau(
#         monitor="val_loss", factor=0.5, patience=8, min_lr=1e-6, verbose=1
#     ),
# ]

# history = model.fit(
#     X_train, y_train,
#     epochs=200,
#     batch_size=32,
#     validation_data=(X_test, y_test),
#     class_weight=class_weight,   # ← key fix cho threshold/spike imbalance
#     callbacks=callbacks,
#     verbose=1
# )
# print()

# # ── 7. Evaluate ───────────────────────────────────────────────────────────────
# print("=" * 55)
# print("5. EVALUATION")
# print("=" * 55)

# y_pred_proba = model.predict(X_test)
# y_pred       = np.argmax(y_pred_proba, axis=1)

# report = classification_report(
#     y_test, y_pred,
#     target_names=["normal", "threshold_anomaly", "spike_anomaly"]
# )
# print(report)

# # Lưu report ra file text
# with open(PREFIX + "_report.txt", "w") as f:
#     f.write(report)

# # ── 8. Plot ───────────────────────────────────────────────────────────────────
# cm  = confusion_matrix(y_test, y_pred)
# fig, axes = plt.subplots(1, 2, figsize=(14, 5))

# disp = ConfusionMatrixDisplay(cm, display_labels=["normal", "threshold", "spike"])
# disp.plot(ax=axes[0], colorbar=False)
# axes[0].set_title("Confusion Matrix — v2")

# ax = axes[1]
# ax.plot(history.history["loss"],        label="Train loss",  color="#1f77b4")
# ax.plot(history.history["val_loss"],    label="Val loss",    color="#ff7f0e")
# ax.plot(history.history["accuracy"],    label="Train acc",   color="#2ca02c", linestyle="--")
# ax.plot(history.history["val_accuracy"],label="Val acc",     color="#d62728", linestyle="--")

# # Đánh dấu epoch tốt nhất
# best_epoch = int(np.argmin(history.history["val_loss"]))
# ax.axvline(best_epoch, color="gray", linestyle=":", alpha=0.7,
#            label=f"Best epoch ({best_epoch})")

# ax.set_xlabel("Epoch")
# ax.set_title("Training curves — v2")
# ax.legend()
# ax.grid(True, alpha=0.3)

# plt.tight_layout()
# plt.savefig(PREFIX + "_evaluation.png", dpi=120)
# plt.close()
# print(f"Đã lưu biểu đồ: {PREFIX}_evaluation.png")
# print()

# # ── 9. Save Keras model ───────────────────────────────────────────────────────
# print("=" * 55)
# print("6. EXPORT MODEL")
# print("=" * 55)

# model.save(PREFIX + ".h5")
# print(f"Saved: {PREFIX}.h5")

# # ── 10. Convert TFLite ────────────────────────────────────────────────────────
# converter = tf.lite.TFLiteConverter.from_keras_model(model)
# converter.optimizations = [tf.lite.Optimize.DEFAULT]

# # Representative dataset cho full int8 quantization (tùy chọn — comment nếu không dùng)
# def representative_dataset():
#     for i in range(0, len(X_train), 10):
#         yield [X_train[i:i+1].astype(np.float32)]

# converter.representative_dataset = representative_dataset
# # Uncomment để full int8 (nhỏ hơn nhưng cần kiểm tra accuracy):
# # converter.target_spec.supported_ops = [tf.lite.OpsSet.TFLITE_BUILTINS_INT8]
# # converter.inference_input_type  = tf.int8
# # converter.inference_output_type = tf.int8

# tflite_model = converter.convert()
# tflite_path  = PREFIX + ".tflite"
# with open(tflite_path, "wb") as f:
#     f.write(tflite_model)
# print(f"Saved: {tflite_path} ({len(tflite_model):,} bytes)")

# # ── 11. Verify TFLite accuracy ────────────────────────────────────────────────
# interpreter = tf.lite.Interpreter(model_content=tflite_model)
# interpreter.allocate_tensors()
# inp  = interpreter.get_input_details()[0]
# out  = interpreter.get_output_details()[0]

# tflite_preds = []
# for sample in X_test.astype(np.float32):
#     interpreter.set_tensor(inp["index"], sample.reshape(1, -1))
#     interpreter.invoke()
#     tflite_preds.append(np.argmax(interpreter.get_tensor(out["index"])))

# tflite_acc = np.mean(np.array(tflite_preds) == y_test)
# print(f"TFLite accuracy (post-quantization): {tflite_acc*100:.2f}%")
# print()

# # ── 12. Generate C header ─────────────────────────────────────────────────────
# header_path = PREFIX + ".h"
# hex_lines   = [
#     ", ".join([f"0x{b:02x}" for b in tflite_model[i:i+12]])
#     for i in range(0, len(tflite_model), 12)
# ]

# with open(header_path, "w") as hf:
#     hf.write("// Auto-generated by train_model_v2.py\n")
#     hf.write("// DHT20 Anomaly Detector v2 — TFLite Micro\n\n")
#     hf.write("#pragma once\n\n")
#     hf.write("// ── Scaler constants (MinMaxScaler) ──────────────────────────\n")
#     hf.write("// normalized = (x - MIN) / (MAX - MIN)\n\n")
#     for i, feat in enumerate(FEATURES):
#         safe = feat.upper()
#         hf.write(f"const float SCALER_{safe}_MIN = {scaler.data_min_[i]:.4f}f;\n")
#         hf.write(f"const float SCALER_{safe}_MAX = {scaler.data_max_[i]:.4f}f;\n")

#     hf.write("\n// ── Normalize macro (dùng trong loop inference) ─────────────\n")
#     hf.write("// #define NORM(x, feat) ((x - SCALER_##feat##_MIN) / (SCALER_##feat##_MAX - SCALER_##feat##_MIN))\n\n")

#     hf.write("// ── Label mapping ─────────────────────────────────────────────\n")
#     hf.write("// 0 = NORMAL\n// 1 = ANOMALY_THRESHOLD\n// 2 = ANOMALY_SPIKE\n\n")

#     hf.write("// ── Model weights ─────────────────────────────────────────────\n")
#     hf.write(f"const unsigned int model_len = {len(tflite_model)};\n\n")
#     hf.write("const unsigned char model[] = {\n  ")
#     hf.write(",\n  ".join(hex_lines))
#     hf.write("\n};\n")

# print(f"Saved: {header_path}")
# print()

# # ── 13. ESP32 firmware checklist ──────────────────────────────────────────────
# print("=" * 55)
# print("7. ESP32 FIRMWARE CHECKLIST")
# print("=" * 55)
# print(f"  Input shape : ({len(FEATURES)},)")
# print(f"  Features    : {FEATURES}")
# print(f"  Output shape: ({NUM_CLASSES},)  → [normal, threshold, spike]")
# print()
# print("  Arduino C++ normalize snippet:")
# for feat in FEATURES:
#     s = feat.upper()
#     print(f"    float norm_{feat:20s} = (raw_{feat} - SCALER_{s}_MIN) / (SCALER_{s}_MAX - SCALER_{s}_MIN);")
# print()
# print("  rolling_mean_temp / rolling_std_temp:")
# print("    → Cần buffer 5 giá trị temp gần nhất trên ESP32")
# print("    → rolling_mean = avg(buf), rolling_std = stddev(buf)")
# print()
# print("Files đã tạo:")
# for f in [
#     "sensor_data.csv       → Dataset v2 (6 features, label đúng)",
#     f"{PREFIX}.h5           → Keras model",
#     f"{PREFIX}.tflite       → TFLite model",
#     f"{PREFIX}.h            → C header cho ESP32",
#     f"{PREFIX}_scaler.json  → Scaler params",
#     f"{PREFIX}_evaluation.png → Biểu đồ training",
#     f"{PREFIX}_report.txt   → Classification report",
# ]:
#     print(f"  {f}")



"""
train_model.py -- v2
TinyML pipeline for DHT20 Anomaly Detection (3-class).

Improvements over v1:
  - 6 features: temp, humidity, delta_temp, delta_humidity, rolling_mean_temp, rolling_std_temp
  - class_weight balanced  -> fair loss for threshold/spike minority
  - Architecture: 32->16->8->3 + BatchNorm + Dropout
  - EarlyStopping patience=20 + ReduceLROnPlateau
  - TFLite verify after quantization
  - .h header: ASCII-only, no encoding issues
"""

import numpy as np
import pandas as pd
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import json
from sklearn.model_selection import train_test_split
from sklearn.preprocessing import MinMaxScaler
from sklearn.metrics import confusion_matrix, classification_report, ConfusionMatrixDisplay
from sklearn.utils.class_weight import compute_class_weight
import tensorflow as tf

PREFIX = "TinyML_v2"

# -- 1. Load data --------------------------------------------------------------
print("=" * 55)
print("1. LOADING DATA")
print("=" * 55)

df = pd.read_csv("sensor_data.csv")
print(f"Total samples: {len(df)}")
print(df["label"].value_counts().rename({0: "normal", 1: "threshold", 2: "spike"}))
print()

FEATURES = [
    "temp", "humidity",
    "delta_temp", "delta_humidity",
    "rolling_mean_temp", "rolling_std_temp"
]
X = df[FEATURES].values
y = df["label"].values

# -- 2. Normalize --------------------------------------------------------------
print("=" * 55)
print("2. NORMALIZING")
print("=" * 55)

scaler   = MinMaxScaler()
X_scaled = scaler.fit_transform(X)

scaler_params = {
    "features": FEATURES,
    "min"     : scaler.data_min_.tolist(),
    "max"     : scaler.data_max_.tolist(),
    "scale"   : scaler.scale_.tolist()
}
with open(PREFIX + "_scaler.json", "w", encoding="ascii") as f:
    json.dump(scaler_params, f, indent=2)

print("Scaler params:")
for i, feat in enumerate(FEATURES):
    print(f"  {feat:30s}: min={scaler.data_min_[i]:.3f}, max={scaler.data_max_[i]:.3f}")
print()

# -- 3. Train/test split -------------------------------------------------------
X_train, X_test, y_train, y_test = train_test_split(
    X_scaled, y, test_size=0.2, random_state=42, stratify=y
)
print(f"Train: {len(X_train)} | Test: {len(X_test)}")
print()

# -- 4. Class weights ----------------------------------------------------------
classes     = np.unique(y_train)
weights_arr = compute_class_weight("balanced", classes=classes, y=y_train)
class_weight = dict(zip(classes, weights_arr))
print(f"Class weights: {class_weight}")
print()

# -- 5. Build model ------------------------------------------------------------
print("=" * 55)
print("3. BUILDING MODEL")
print("=" * 55)

NUM_CLASSES = 3

model = tf.keras.Sequential([
    tf.keras.layers.Input(shape=(len(FEATURES),)),

    tf.keras.layers.Dense(32),
    tf.keras.layers.BatchNormalization(),
    tf.keras.layers.Activation("relu"),
    tf.keras.layers.Dropout(0.3),

    tf.keras.layers.Dense(16),
    tf.keras.layers.BatchNormalization(),
    tf.keras.layers.Activation("relu"),
    tf.keras.layers.Dropout(0.2),

    tf.keras.layers.Dense(8, activation="relu"),
    tf.keras.layers.Dense(NUM_CLASSES, activation="softmax")
], name="dht20_anomaly_v2")

model.compile(
    loss="sparse_categorical_crossentropy",
    optimizer=tf.keras.optimizers.Adam(learning_rate=1e-3),
    metrics=["accuracy"]
)
model.summary()
print()

# -- 6. Train ------------------------------------------------------------------
print("=" * 55)
print("4. TRAINING")
print("=" * 55)

callbacks = [
    tf.keras.callbacks.EarlyStopping(
        monitor="val_loss", patience=20, restore_best_weights=True, verbose=1
    ),
    tf.keras.callbacks.ReduceLROnPlateau(
        monitor="val_loss", factor=0.5, patience=8, min_lr=1e-6, verbose=1
    ),
]

history = model.fit(
    X_train, y_train,
    epochs=200,
    batch_size=32,
    validation_data=(X_test, y_test),
    class_weight=class_weight,
    callbacks=callbacks,
    verbose=1
)
print()

# -- 7. Evaluate ---------------------------------------------------------------
print("=" * 55)
print("5. EVALUATION")
print("=" * 55)

y_pred_proba = model.predict(X_test)
y_pred       = np.argmax(y_pred_proba, axis=1)

report = classification_report(
    y_test, y_pred,
    target_names=["normal", "threshold_anomaly", "spike_anomaly"]
)
print(report)

with open(PREFIX + "_report.txt", "w", encoding="ascii") as f:
    f.write(report)

# -- 8. Plot -------------------------------------------------------------------
cm  = confusion_matrix(y_test, y_pred)
fig, axes = plt.subplots(1, 2, figsize=(14, 5))

disp = ConfusionMatrixDisplay(cm, display_labels=["normal", "threshold", "spike"])
disp.plot(ax=axes[0], colorbar=False)
axes[0].set_title("Confusion Matrix v2")

ax = axes[1]
ax.plot(history.history["loss"],         label="Train loss",  color="#1f77b4")
ax.plot(history.history["val_loss"],     label="Val loss",    color="#ff7f0e")
ax.plot(history.history["accuracy"],     label="Train acc",   color="#2ca02c", linestyle="--")
ax.plot(history.history["val_accuracy"], label="Val acc",     color="#d62728", linestyle="--")

best_epoch = int(np.argmin(history.history["val_loss"]))
ax.axvline(best_epoch, color="gray", linestyle=":", alpha=0.7,
           label=f"Best epoch ({best_epoch})")
ax.set_xlabel("Epoch")
ax.set_title("Training curves v2")
ax.legend()
ax.grid(True, alpha=0.3)

plt.tight_layout()
plt.savefig(PREFIX + "_evaluation.png", dpi=120)
plt.close()
print(f"Saved: {PREFIX}_evaluation.png")
print()

# -- 9. Save Keras model -------------------------------------------------------
print("=" * 55)
print("6. EXPORT MODEL")
print("=" * 55)

model.save(PREFIX + ".h5")
print(f"Saved: {PREFIX}.h5")

# -- 10. Convert TFLite --------------------------------------------------------
converter = tf.lite.TFLiteConverter.from_keras_model(model)
converter.optimizations = [tf.lite.Optimize.DEFAULT]

def representative_dataset():
    for i in range(0, len(X_train), 10):
        yield [X_train[i:i+1].astype(np.float32)]

converter.representative_dataset = representative_dataset
tflite_model = converter.convert()

tflite_path = PREFIX + ".tflite"
with open(tflite_path, "wb") as f:
    f.write(tflite_model)
print(f"Saved: {tflite_path} ({len(tflite_model):,} bytes)")

# -- 11. Verify TFLite accuracy ------------------------------------------------
interpreter = tf.lite.Interpreter(model_content=tflite_model)
interpreter.allocate_tensors()
inp = interpreter.get_input_details()[0]
out = interpreter.get_output_details()[0]

tflite_preds = []
for sample in X_test.astype(np.float32):
    interpreter.set_tensor(inp["index"], sample.reshape(1, -1))
    interpreter.invoke()
    tflite_preds.append(np.argmax(interpreter.get_tensor(out["index"])))

tflite_acc = np.mean(np.array(tflite_preds) == y_test)
print(f"TFLite accuracy (post-quantization): {tflite_acc*100:.2f}%")
print()

# -- 12. Generate C header (ASCII only) ----------------------------------------
header_path = PREFIX + ".h"
hex_lines = [
    ", ".join([f"0x{b:02x}" for b in tflite_model[i:i+12]])
    for i in range(0, len(tflite_model), 12)
]

with open(header_path, "w", encoding="ascii") as hf:
    hf.write("// Auto-generated by train_model_v2.py\n")
    hf.write("// DHT20 Anomaly Detector v2 - TFLite Micro\n\n")
    hf.write("#pragma once\n\n")
    hf.write("// Scaler constants (MinMaxScaler)\n")
    hf.write("// Formula: normalized = (x - MIN) / (MAX - MIN)\n\n")
    for i, feat in enumerate(FEATURES):
        safe = feat.upper()
        hf.write(f"const float SCALER_{safe}_MIN = {scaler.data_min_[i]:.4f}f;\n")
        hf.write(f"const float SCALER_{safe}_MAX = {scaler.data_max_[i]:.4f}f;\n")
    hf.write("\n// Label mapping\n")
    hf.write("// 0 = NORMAL\n")
    hf.write("// 1 = ANOMALY_THRESHOLD\n")
    hf.write("// 2 = ANOMALY_SPIKE\n\n")
    hf.write("// Model weights\n")
    hf.write(f"const unsigned int model_len = {len(tflite_model)};\n\n")
    hf.write("const unsigned char model[] = {\n  ")
    hf.write(",\n  ".join(hex_lines))
    hf.write("\n};\n")

print(f"Saved: {header_path}")

# Quick verify header is not empty
import os
size = os.path.getsize(header_path)
print(f"Header file size: {size:,} bytes  {'OK' if size > 1000 else 'WARNING: too small!'}")
print()

# -- 13. ESP32 checklist -------------------------------------------------------
print("=" * 55)
print("7. ESP32 FIRMWARE CHECKLIST")
print("=" * 55)
print(f"  Input  : ({len(FEATURES)},) -> {FEATURES}")
print(f"  Output : ({NUM_CLASSES},)  -> [normal, threshold, spike]")
print()
print("  Normalize snippet (Arduino C++):")
for feat in FEATURES:
    s = feat.upper()
    print(f"    float norm_{feat} = (raw_{feat} - SCALER_{s}_MIN) / (SCALER_{s}_MAX - SCALER_{s}_MIN);")
print()
print("  Rolling buffer (5 samples) needed for rolling_mean_temp / rolling_std_temp")
print()
print("Files created:")
print(f"  sensor_data.csv        : Dataset (6 features)")
print(f"  {PREFIX}.h5            : Keras model")
print(f"  {PREFIX}.tflite        : TFLite model")
print(f"  {PREFIX}.h             : C header for ESP32 (ASCII-safe)")
print(f"  {PREFIX}_scaler.json   : Scaler params")
print(f"  {PREFIX}_evaluation.png: Training plots")
print(f"  {PREFIX}_report.txt    : Classification report")