import paho.mqtt.client as mqtt
import json
import time

# --------------------------------------
# Configuration
# --------------------------------------
THINGSBOARD_HOST = 'app.coreiot.io'  # replace with your gateway address
THINGSBOARD_PORT = 1883  # default is 1883 for non-TLS
ACCESS_TOKEN = 'rHa5SAQEanvnWzXnqDNi'  # replace with your device access token


# --------------------------------------
# MQTT Callback functions (optional, for logging)
# --------------------------------------
def on_connect(client, userdata, flags, rc):
    if rc == 0:
        print("Connected to ThingsBoard!")
    else:
        print("Failed to connect, return code %d\n", rc)


def on_publish(client, userdata, mid):
    print("Message published!")


# --------------------------------------
# Setup MQTT client
# --------------------------------------
client = mqtt.Client()
client.username_pw_set(ACCESS_TOKEN)
client.on_connect = on_connect
client.on_publish = on_publish

client.connect(THINGSBOARD_HOST, THINGSBOARD_PORT, 60)
client.loop_start()

# --------------------------------------
# Publish Data Loop
# --------------------------------------
try:
    while True:
        # Example telemetry payload

        telemetry = {
            "ESP32_001": [
                {"ts": int(time.time() * 1000), "values": {"temperature": 22.5, "humidity": 55}}
            ],
            "ESP32_002": [
                {"ts": int(time.time() * 1000), "values": {"temperature": 30.5, "humidity": 80}}
            ],
            "ESP32_003": [
                {"ts": int(time.time() * 1000), "values": {"temperature": 10.5, "humidity": 20}}
            ]
        }

        # Convert to JSON and publish!
        payload = json.dumps(telemetry)
        # Telemetry publish topic in ThingsBoard
        result = client.publish('v1/gateway/telemetry', payload)

        time.sleep(5)  # publish every 5 seconds

except KeyboardInterrupt:
    print('Interrupted!')

finally:
    client.loop_stop()
    client.disconnect()