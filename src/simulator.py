# #!/usr/bin/env python3
# """
# IoT Sensor Simulator + CoreIOT MQTT
# ESP32 Serial Log Style Version (Realistic Environment Simulation)
# """
# import random
# import time
# import json
# from enum import Enum
# import paho.mqtt.client as mqtt

# # =========================================================
# # MQTT CONFIG
# # =========================================================
# BROKER = "app.coreiot.io"
# PORT = 1883
# TOKEN = "wjlbjhro9k1f2b5f6t0b"

# TOPIC_TELEMETRY = "v1/devices/me/telemetry"
# TOPIC_RPC = "v1/devices/me/rpc/request/+"

# # =========================================================
# # ENUMS
# # =========================================================
# class TempLevel(Enum):
#     TEMP_NORMAL = 0
#     TEMP_WARNING = 1
#     TEMP_CRITICAL = 2

# class HumiLevel(Enum):
#     HUMI_DRY = 0
#     HUMI_NORMAL = 1
#     HUMI_WET = 2

# class LCDState(Enum):
#     LCD_NORMAL = 0
#     LCD_WARNING = 1
#     LCD_CRITICAL = 2

# # =========================================================
# # CLASSIFY FUNCTIONS
# # =========================================================
# def classify_state(temp, humidity):
#     if temp >= 35.0 or humidity < 25.0 or humidity > 85.0:
#         return LCDState.LCD_CRITICAL
#     if (30.0 <= temp < 35.0) or humidity < 40.0 or humidity > 70.0:
#         return LCDState.LCD_WARNING
#     return LCDState.LCD_NORMAL

# def classify_humidity(humidity):
#     if humidity < 40.0:
#         return HumiLevel.HUMI_DRY
#     if humidity <= 70.0:
#         return HumiLevel.HUMI_NORMAL
#     return HumiLevel.HUMI_WET

# def classify_temperature(temp):
#     if temp >= 35.0:
#         return TempLevel.TEMP_CRITICAL
#     if temp >= 30.0:
#         return TempLevel.TEMP_WARNING
#     return TempLevel.TEMP_NORMAL

# # =========================================================
# # REALISTIC SENSOR SIMULATION
# # =========================================================
# def generate_realistic_data(counter, current_temp, current_humi):
#     """
#     Mô phỏng:
#     - 0–15: môi trường điều hòa ổn định
#     - 16–22: người dùng thổi nóng vào cảm biến
#     - 23+: nhiệt độ hạ dần về bình thường
#     """

#     base_temp = 25.0
#     base_humi = 50.0

#     # ============================================
#     # STABLE AIR-CONDITIONED ROOM
#     # ============================================
#     if counter <= 15:
#         current_temp += random.gauss(0, 0.15)
#         current_humi += random.gauss(0, 0.8)

#     # ============================================
#     # USER BREATHES HOT AIR INTO SENSOR
#     # ============================================
#     elif counter <= 22:
#         current_temp += random.uniform(1.2, 2.0)
#         current_humi += random.uniform(1.0, 3.0)

#     # ============================================
#     # SENSOR COOLDOWN
#     # ============================================
#     else:
#         current_temp -= (current_temp - base_temp) * random.uniform(0.15, 0.25)
#         current_humi -= (current_humi - base_humi) * random.uniform(0.10, 0.20)

#         current_temp += random.gauss(0, 0.1)
#         current_humi += random.gauss(0, 0.5)

#     # Clamp
#     current_temp = max(15.0, min(40.0, current_temp))
#     current_humi = max(20.0, min(90.0, current_humi))

#     return (
#         round(current_temp, 1),
#         round(current_humi, 1),
#         current_temp,
#         current_humi,
#     )

# # =========================================================
# # MQTT CALLBACKS
# # =========================================================
# def on_connect(client, userdata, flags, rc, properties=None):
#     if rc == 0:
#         print("Connected to ThingsBoard/CoreIOT!")
#         client.subscribe(TOPIC_RPC)
#         print(f"Subscribed: {TOPIC_RPC}")
#     else:
#         print("Connection failed, return code:", rc)

# def on_message(client, userdata, msg):
#     print("\n========== RPC RECEIVED ==========")
#     print("Topic:", msg.topic)

#     try:
#         payload = msg.payload.decode()
#         print("Payload:", payload)

#         data = json.loads(payload)
#         method = data.get("method")

#         if method == "setStateLED":
#             if str(data.get("params")).upper() == "ON":
#                 print("LED STATUS -> ON")
#             else:
#                 print("LED STATUS -> OFF")
#         else:
#             print("Unknown method")

#     except Exception as e:
#         print("RPC Parse Error:", e)

#     print("==================================")

# # =========================================================
# # MAIN
# # =========================================================
# def main():
#     print("=" * 60)
#     print("ESP32 IoT Sensor Simulator - Firmware Accurate Log")
#     print("=" * 60)

#     current_temp = 25.0
#     current_humi = 50.0

#     client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
#     client.username_pw_set(TOKEN)

#     client.on_connect = on_connect
#     client.on_message = on_message

#     try:
#         print("Connecting to broker...")
#         client.connect(BROKER, PORT, 60)
#         client.loop_start()
#     except Exception as e:
#         print("MQTT Connection Error:", e)
#         return

#     last_humi_level = None
#     last_temp_level = None
#     last_lcd_state = None
#     counter = 0

#     try:
#         while True:
#             temperature, humidity, current_temp, current_humi = generate_realistic_data(
#                 counter,
#                 current_temp,
#                 current_humi
#             )

#             lcd_state = classify_state(temperature, humidity)
#             humi_level = classify_humidity(humidity)
#             temp_level = classify_temperature(temperature)

#             # =====================================================
#             # NeoPixel Firmware Log
#             # =====================================================
#             if humi_level != last_humi_level:
#                 if humi_level == HumiLevel.HUMI_DRY:
#                     print("[NEO] DRY -> RED FAST")
#                 elif humi_level == HumiLevel.HUMI_NORMAL:
#                     print("[NEO] NORMAL -> YELLOW")
#                 elif humi_level == HumiLevel.HUMI_WET:
#                     print("[NEO] WET -> GREEN SLOW")

#                 last_humi_level = humi_level

#             # =====================================================
#             # LED Firmware Log
#             # =====================================================
#             if temp_level != last_temp_level:
#                 if temp_level == TempLevel.TEMP_NORMAL:
#                     print("[LED] NORMAL -> SLOW BLINK")
#                 elif temp_level == TempLevel.TEMP_WARNING:
#                     print("[LED] WARNING -> MEDIUM BLINK")
#                 elif temp_level == TempLevel.TEMP_CRITICAL:
#                     print("[LED] CRITICAL -> FAST BLINK")

#                 last_temp_level = temp_level

#             # =====================================================
#             # LCD Firmware Log
#             # =====================================================
#             if lcd_state != last_lcd_state:
#                 if lcd_state == LCDState.LCD_NORMAL:
#                     print("[LCD] NORMAL MODE")
#                 elif lcd_state == LCDState.LCD_WARNING:
#                     print("[LCD] WARNING MODE")
#                 elif lcd_state == LCDState.LCD_CRITICAL:
#                     print("[LCD] CRITICAL MODE")

#                 last_lcd_state = lcd_state

#             # =====================================================
#             # Main Sensor Serial Output
#             # =====================================================
#             humi_text = humi_level.name.replace("HUMI_", "")

#             print(
#                 f"Temp: {temperature:.1f} C | "
#                 f"Humi: {humidity:.1f} % | "
#                 f"Level: {humi_text}"
#             )

#             # =====================================================
#             # MQTT Telemetry Publish
#             # =====================================================
#             payload = {
#                 "temperature": temperature,
#                 "humidity": humidity
#             }

#             result = client.publish(
#                 TOPIC_TELEMETRY,
#                 json.dumps(payload)
#             )

#             if result.rc != 0:
#                 print("Publish Failed")

#             counter += 1

#             # giống firmware FreeRTOS
#             time.sleep(2)

#     except KeyboardInterrupt:
#         print("\nStopped by user")

#     finally:
#         client.loop_stop()
#         client.disconnect()
#         print("Disconnected from broker")

# # =========================================================
# # RUN
# # =========================================================
# if __name__ == "__main__":
#     main()





















#!/usr/bin/env python3
"""
ESP32 IoT Firmware Simulator
CoreIOT MQTT + RPC Control
"""

import json
import random
import time
from enum import Enum

import paho.mqtt.client as mqtt

# =========================================================
# MQTT CONFIG
# =========================================================
BROKER = "app.coreiot.io"
PORT = 1883
TOKEN = "wjlbjhro9k1f2b5f6t0b"

TOPIC_TELEMETRY = "v1/devices/me/telemetry"
TOPIC_RPC = "v1/devices/me/rpc/request/+"

# =========================================================
# ENUMS
# =========================================================
class TempLevel(Enum):
    TEMP_NORMAL = 0
    TEMP_WARNING = 1
    TEMP_CRITICAL = 2


class HumiLevel(Enum):
    HUMI_DRY = 0
    HUMI_NORMAL = 1
    HUMI_WET = 2


class LCDState(Enum):
    LCD_NORMAL = 0
    LCD_WARNING = 1
    LCD_CRITICAL = 2


# =========================================================
# GLOBAL DEVICE STATES
# =========================================================
led_state = False
fan_state = False
fan_speed = 0

rgb_r = 0
rgb_g = 0
rgb_b = 0

mqtt_connected = False

# =========================================================
# CLASSIFY FUNCTIONS
# =========================================================
def classify_state(temp, humidity):

    if temp >= 35.0 or humidity < 25.0 or humidity > 85.0:
        return LCDState.LCD_CRITICAL

    if (30.0 <= temp < 35.0) or humidity < 40.0 or humidity > 70.0:
        return LCDState.LCD_WARNING

    return LCDState.LCD_NORMAL


def classify_humidity(humidity):

    if humidity < 40.0:
        return HumiLevel.HUMI_DRY

    if humidity <= 70.0:
        return HumiLevel.HUMI_NORMAL

    return HumiLevel.HUMI_WET


def classify_temperature(temp):

    if temp >= 35.0:
        return TempLevel.TEMP_CRITICAL

    if temp >= 30.0:
        return TempLevel.TEMP_WARNING

    return TempLevel.TEMP_NORMAL


# =========================================================
# SENSOR SIMULATION
# =========================================================
def generate_realistic_data(counter, current_temp, current_humi):

    base_temp = 25.0
    base_humi = 50.0

    # =====================================================
    # STABLE ROOM
    # =====================================================
    if counter <= 15:

        current_temp += random.gauss(0, 0.15)
        current_humi += random.gauss(0, 0.8)

    # =====================================================
    # HOT AIR
    # =====================================================
    elif counter <= 22:

        current_temp += random.uniform(1.2, 2.0)
        current_humi += random.uniform(1.0, 3.0)

    # =====================================================
    # COOLDOWN
    # =====================================================
    else:

        current_temp -= (
            current_temp - base_temp
        ) * random.uniform(0.15, 0.25)

        current_humi -= (
            current_humi - base_humi
        ) * random.uniform(0.10, 0.20)

        current_temp += random.gauss(0, 0.1)
        current_humi += random.gauss(0, 0.5)

    # =====================================================
    # CLAMP
    # =====================================================
    current_temp = max(15.0, min(40.0, current_temp))
    current_humi = max(20.0, min(90.0, current_humi))

    return (
        round(current_temp, 1),
        round(current_humi, 1),
        current_temp,
        current_humi,
    )


# =========================================================
# MQTT CALLBACKS
# =========================================================
def on_connect(client, userdata, flags, rc, properties=None):

    global mqtt_connected

    if rc == 0:

        mqtt_connected = True

        print("\n======================================")
        print("Connected to CoreIOT MQTT Broker")
        print("======================================")

        client.subscribe(TOPIC_RPC)

        print(f"Subscribed RPC Topic: {TOPIC_RPC}")

    else:

        print(f"[MQTT] Connection Failed -> rc={rc}")


def on_disconnect(client, userdata, rc, properties=None):

    global mqtt_connected

    mqtt_connected = False

    print(f"\n[MQTT] Disconnected -> rc={rc}")


# =========================================================
# RPC HANDLER
# =========================================================
def on_message(client, userdata, msg):

    global led_state
    global fan_state
    global fan_speed

    global rgb_r
    global rgb_g
    global rgb_b

    print("\n========== RPC RECEIVED ==========")

    try:

        payload = msg.payload.decode()

        print("Topic  :", msg.topic)
        print("Payload:", payload)

        data = json.loads(payload)

        method = data.get("method")
        params = data.get("params", {})

        # =================================================
        # LED CONTROL
        # =================================================
        if method == "setLed":

            led_state = bool(params.get("value", False))

            if led_state:
                print("[RPC] setLed received -> ON")
            else:
                print("[RPC] setLed received -> OFF")
        elif method == "getLed":

            print("[RPC] getLed received")

            response_topic = msg.topic.replace(
                "request",
                "response"
            )

            client.publish(
                response_topic,
                json.dumps(led_state)
            )
        # =================================================
        # FAN CONTROL
        # =================================================
        elif method == "setFan":

            # =================================================
            # CASE 1:
            # params = true / false
            # =================================================
            if isinstance(params, bool):

                fan_state = params

            # =================================================
            # CASE 2:
            # params = {"value": true}
            # =================================================
            elif isinstance(params, dict):

                fan_state = bool(
                    params.get("value", False)
                )

            else:

                fan_state = False

            if fan_state:
                print("[RPC] setFan received -> ON")
            else:
                print("[RPC] setFan received -> OFF")
        elif method == "getFan":

            print("[RPC] getFan received")

            response_topic = msg.topic.replace(
                "request",
                "response"
            )

            client.publish(
                response_topic,
                json.dumps(fan_state)
            )
        # =================================================
        # FAN SPEED
        # =================================================
        elif method == "setFanSpeed":

            # ============================================
            # CASE 1:
            # params = 70
            # ============================================
            if isinstance(params, (int, float)):

                fan_speed = int(params)

            # ============================================
            # CASE 2:
            # params = {"value":70}
            # ============================================
            elif isinstance(params, dict):

                fan_speed = int(
                    params.get("value", 0)
                )

            # ============================================
            # UNKNOWN FORMAT
            # ============================================
            else:

                fan_speed = 0

            # clamp
            fan_speed = max(0, min(100, fan_speed))

            print(
                f"[RPC] setFanSpeed received -> "
                f"{fan_speed}%"
            )
        elif method == "getFanSpeed":

            print("[RPC] getFanSpeed received")

            response_topic = msg.topic.replace(
                "request",
                "response"
            )

            client.publish(
                response_topic,
                json.dumps(fan_speed)
            )
        # =================================================
        # RGB CONTROL
        # =================================================
        elif method == "setRgb":

            rgb_r = int(params.get("r", 0))
            rgb_g = int(params.get("g", 0))
            rgb_b = int(params.get("b", 0))

            rgb_r = max(0, min(255, rgb_r))
            rgb_g = max(0, min(255, rgb_g))
            rgb_b = max(0, min(255, rgb_b))

            print(
                f"[RPC] setRgb received -> "
                f"R={rgb_r} G={rgb_g} B={rgb_b}"
            )

        # =================================================
        # UNKNOWN METHOD
        # =================================================
        else:

            print(f"[RPC] Unknown method -> {method}")

    except Exception as e:

        print("RPC Parse Error:", e)

    print("==================================")


# =========================================================
# MAIN
# =========================================================
def main():

    global mqtt_connected

    print("=" * 60)
    print("ESP32 IoT Firmware Simulator")
    print("=" * 60)

    current_temp = 25.0
    current_humi = 50.0

    # =====================================================
    # MQTT CLIENT
    # =====================================================
    client = mqtt.Client(
        mqtt.CallbackAPIVersion.VERSION2
    )

    client.username_pw_set(TOKEN)

    client.on_connect = on_connect
    client.on_disconnect = on_disconnect
    client.on_message = on_message

    client.reconnect_delay_set(
        min_delay=1,
        max_delay=5
    )

    try:

        print("Connecting to MQTT Broker...")

        client.connect(
            BROKER,
            PORT,
            keepalive=120
        )

        client.loop_start()

    except Exception as e:

        print("MQTT Connection Error:", e)
        return

    # =====================================================
    # WAIT FOR MQTT CONNECTION
    # =====================================================
    timeout = 10

    while not mqtt_connected and timeout > 0:

        print("Waiting MQTT Connection...")

        time.sleep(1)

        timeout -= 1

    if not mqtt_connected:

        print("MQTT Connection Timeout")
        return

    # =====================================================
    # STATE TRACKERS
    # =====================================================
    last_humi_level = None
    last_temp_level = None
    last_lcd_state = None

    counter = 0

    # =====================================================
    # MAIN LOOP
    # =====================================================
    try:

        while True:

            (
                temperature,
                humidity,
                current_temp,
                current_humi,
            ) = generate_realistic_data(
                counter,
                current_temp,
                current_humi
            )

            lcd_state = classify_state(
                temperature,
                humidity
            )

            humi_level = classify_humidity(
                humidity
            )

            temp_level = classify_temperature(
                temperature
            )

            # =================================================
            # NEO PIXEL LOG
            # =================================================
            if humi_level != last_humi_level:

                if humi_level == HumiLevel.HUMI_DRY:
                    print("[NEO] DRY -> RED FAST")

                elif humi_level == HumiLevel.HUMI_NORMAL:
                    print("[NEO] NORMAL -> YELLOW")

                elif humi_level == HumiLevel.HUMI_WET:
                    print("[NEO] WET -> GREEN SLOW")

                last_humi_level = humi_level

            # =================================================
            # LED LOG
            # =================================================
            if temp_level != last_temp_level:

                if temp_level == TempLevel.TEMP_NORMAL:
                    print("[LED] NORMAL -> SLOW BLINK")

                elif temp_level == TempLevel.TEMP_WARNING:
                    print("[LED] WARNING -> MEDIUM BLINK")

                elif temp_level == TempLevel.TEMP_CRITICAL:
                    print("[LED] CRITICAL -> FAST BLINK")

                last_temp_level = temp_level

            # =================================================
            # LCD LOG
            # =================================================
            if lcd_state != last_lcd_state:

                if lcd_state == LCDState.LCD_NORMAL:
                    print("[LCD] NORMAL MODE")

                elif lcd_state == LCDState.LCD_WARNING:
                    print("[LCD] WARNING MODE")

                elif lcd_state == LCDState.LCD_CRITICAL:
                    print("[LCD] CRITICAL MODE")

                last_lcd_state = lcd_state

            # =================================================
            # SERIAL OUTPUT
            # =================================================
            print("\n----------------------------------------")

            print(f"Temp : {temperature:.1f} C")
            print(f"Humi : {humidity:.1f} %")

            print(
                f"LED  : {'ON' if led_state else 'OFF'}"
            )

            print(
                f"FAN  : {'ON' if fan_state else 'OFF'}"
            )

            print(f"SPEED: {fan_speed}%")

            print(
                f"RGB  : R={rgb_r} "
                f"G={rgb_g} "
                f"B={rgb_b}"
            )

            # =================================================
            # MQTT TELEMETRY
            # =================================================
            payload = {

                # sensors
                "temperature": temperature,
                "humidity": humidity,

                # devices
                "led_state": led_state,
                "fan_state": fan_state,
                "fan_speed": fan_speed,

                # rgb
                "rgb_r": rgb_r,
                "rgb_g": rgb_g,
                "rgb_b": rgb_b,

                # states
                "temp_level": temp_level.name,
                "humi_level": humi_level.name,
                "lcd_state": lcd_state.name,
            }

            try:

                msg_info = client.publish(
                    TOPIC_TELEMETRY,
                    json.dumps(payload),
                    qos=0
                )

                msg_info.wait_for_publish()

                if msg_info.is_published():

                    print("Telemetry Published")

                else:

                    print("Publish Failed")

            except Exception as e:

                print("Publish Exception:", e)

            counter += 1

            time.sleep(2)

    except KeyboardInterrupt:

        print("\nStopped by user")

    finally:

        client.loop_stop()

        client.disconnect()

        print("Disconnected from MQTT Broker")


# =========================================================
# RUN
# =========================================================
if __name__ == "__main__":
    main()