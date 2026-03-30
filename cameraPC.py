from flask import Flask, Response
import cv2

app = Flask(__name__)
cap = cv2.VideoCapture(0)

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

@app.route('/status')
def status():
    return {"ok": True, "camera": True}

app.run(host='0.0.0.0', port=5000, threaded=True)