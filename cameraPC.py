from flask import Flask, Response, jsonify, request
import cv2
import numpy as np

app = Flask(__name__)
cap = cv2.VideoCapture(0)
GRAY_SIZE = 96

current_source = "camera"
uploaded_frame = None


def add_no_cache_headers(response):
    response.headers["Cache-Control"] = "no-store"
    response.headers["Access-Control-Allow-Origin"] = "*"
    response.headers["Access-Control-Allow-Headers"] = "*"
    response.headers["Access-Control-Allow-Methods"] = "GET, POST, OPTIONS"
    return response


@app.after_request
def apply_headers(response):
    return add_no_cache_headers(response)


def normalize_to_bgr(frame):
    if frame is None:
        return None

    if len(frame.shape) == 2:
        return cv2.cvtColor(frame, cv2.COLOR_GRAY2BGR)

    if frame.shape[2] == 4:
        return cv2.cvtColor(frame, cv2.COLOR_BGRA2BGR)

    return frame


def get_camera_frame():
    success, frame = cap.read()
    if not success:
        return None
    return frame


def get_active_frame():
    global uploaded_frame

    if current_source == "upload" and uploaded_frame is not None:
        return uploaded_frame.copy()

    return get_camera_frame()


def build_gray_snapshot(frame):
    height, width = frame.shape[:2]
    side = min(width, height)
    x0 = (width - side) // 2
    y0 = (height - side) // 2
    square = frame[y0:y0 + side, x0:x0 + side]
    gray = cv2.cvtColor(square, cv2.COLOR_BGR2GRAY)
    resized = cv2.resize(gray, (GRAY_SIZE, GRAY_SIZE), interpolation=cv2.INTER_AREA)
    return resized


def encode_jpg(frame):
    ret, buffer = cv2.imencode(".jpg", frame)
    if not ret:
        return None
    return buffer.tobytes()


def gen_frames():
    while True:
        frame = get_active_frame()
        if frame is None:
            continue

        jpg = encode_jpg(frame)
        if jpg is None:
            continue

        yield (
            b"--frame\r\n"
            b"Content-Type: image/jpeg\r\n\r\n" + jpg + b"\r\n"
        )


@app.route("/video_feed")
def video_feed():
    return Response(gen_frames(), mimetype="multipart/x-mixed-replace; boundary=frame")


@app.route("/snapshot")
def snapshot():
    frame = get_active_frame()
    if frame is None:
        return "camera error", 500

    jpg = encode_jpg(frame)
    if jpg is None:
        return "encode error", 500

    return Response(jpg, mimetype="image/jpeg")


@app.route("/current_frame")
def current_frame():
    frame = get_active_frame()
    if frame is None:
        return "camera error", 500

    jpg = encode_jpg(frame)
    if jpg is None:
        return "encode error", 500

    return Response(jpg, mimetype="image/jpeg")


@app.route("/snapshot_gray")
def snapshot_gray():
    frame = get_active_frame()
    if frame is None:
        return "camera error", 500

    gray = build_gray_snapshot(frame)
    response = Response(gray.tobytes(), mimetype="application/octet-stream")
    response.headers["X-Width"] = str(GRAY_SIZE)
    response.headers["X-Height"] = str(GRAY_SIZE)
    return response


@app.route("/upload_image", methods=["POST", "OPTIONS"])
def upload_image():
    global uploaded_frame
    global current_source

    if request.method == "OPTIONS":
        return ("", 204)

    upload = request.files.get("image") or request.files.get("file")
    if upload is not None:
        file_bytes = upload.read()
    else:
        file_bytes = request.get_data()

    if not file_bytes:
        return jsonify({"ok": False, "error": "empty file"}), 400

    np_buffer = np.frombuffer(file_bytes, dtype=np.uint8)
    decoded = cv2.imdecode(np_buffer, cv2.IMREAD_UNCHANGED)
    if decoded is None:
        return jsonify({"ok": False, "error": "cannot decode image"}), 400

    uploaded_frame = normalize_to_bgr(decoded)
    current_source = "upload"
    return jsonify({"ok": True, "source": current_source})


@app.route("/capture_frame", methods=["POST", "OPTIONS"])
def capture_frame():
    global uploaded_frame
    global current_source

    if request.method == "OPTIONS":
        return ("", 204)

    frame = get_camera_frame()
    if frame is None:
        return jsonify({"ok": False, "error": "camera not ready"}), 500

    uploaded_frame = frame.copy()
    current_source = "upload"
    return jsonify({"ok": True, "source": current_source, "captured": True})


@app.route("/source", methods=["GET", "POST", "OPTIONS"])
def source():
    global current_source

    if request.method == "OPTIONS":
        return ("", 204)

    source_name = request.values.get("name", "").strip().lower()
    if source_name in {"camera", "upload"}:
        if source_name == "upload" and uploaded_frame is None:
            return jsonify({"ok": False, "error": "no uploaded image"}), 400
        current_source = source_name

    return jsonify(
        {
            "ok": True,
            "source": current_source,
            "has_upload": uploaded_frame is not None,
        }
    )


@app.route("/status")
def status():
    return {
        "ok": True,
        "camera": cap.isOpened(),
        "source": current_source,
        "has_upload": uploaded_frame is not None,
    }


app.run(host="0.0.0.0", port=5000, threaded=True)
