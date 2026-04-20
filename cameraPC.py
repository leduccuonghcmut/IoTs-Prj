from flask import Flask, Response
import cv2

app = Flask(__name__)
cap = cv2.VideoCapture(0)
GRAY_SIZE = 96


def build_gray_snapshot(frame):
    height, width = frame.shape[:2]
    side = min(width, height)
    x0 = (width - side) // 2
    y0 = (height - side) // 2
    square = frame[y0:y0 + side, x0:x0 + side]
    gray = cv2.cvtColor(square, cv2.COLOR_BGR2GRAY)
    resized = cv2.resize(gray, (GRAY_SIZE, GRAY_SIZE), interpolation=cv2.INTER_AREA)
    return resized

def gen_frames():
    while True:
        success, frame = cap.read()
        if not success:
            continue

        ret, buffer = cv2.imencode('.jpg', frame)
        if not ret:
            continue

        jpg = buffer.tobytes()
        yield (b'--frame\r\n'
               b'Content-Type: image/jpeg\r\n\r\n' + jpg + b'\r\n')

@app.route('/video_feed')
def video_feed():
    return Response(gen_frames(),
                    mimetype='multipart/x-mixed-replace; boundary=frame')

@app.route('/snapshot')
def snapshot():
    success, frame = cap.read()
    if not success:
        return "camera error", 500

    ret, buffer = cv2.imencode('.jpg', frame)
    if not ret:
        return "encode error", 500

    return Response(buffer.tobytes(), mimetype='image/jpeg')


@app.route('/snapshot_gray')
def snapshot_gray():
    success, frame = cap.read()
    if not success:
        return "camera error", 500

    gray = build_gray_snapshot(frame)
    response = Response(gray.tobytes(), mimetype='application/octet-stream')
    response.headers['X-Width'] = str(GRAY_SIZE)
    response.headers['X-Height'] = str(GRAY_SIZE)
    response.headers['Cache-Control'] = 'no-store'
    return response

@app.route('/status')
def status():
    return {"ok": True, "camera": True}

app.run(host='0.0.0.0', port=5000, threaded=True)
