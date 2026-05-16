const POLL_SENSORS_MS = 2000;
const POLL_STATE_MS = 3000;
const POLL_WIFI_MS = 3000;
const POLL_CAMERA_MS = 4000;
let rgbPickerDirty = false;
let rgbPickerDraftHex = "#000000";
let remoteRgbPickerDirty = false;
let remoteRgbPickerDraftHex = "#000000";

function getCameraHost() {
  return document.getElementById("pcIpInput")?.value?.trim() || localStorage.getItem("pc_ip") || "";
}

function getCameraBaseUrl() {
  const host = getCameraHost();
  return host ? `http://${host}` : "";
}

async function parseJsonSafe(response) {
  const text = await response.text();
  if (!text) {
    return {};
  }

  try {
    return JSON.parse(text);
  } catch (error) {
    return { raw: text };
  }
}

function setText(id, value) {
  const element = document.getElementById(id);
  if (element) {
    element.textContent = value;
  }
}

function setBooleanBadge(id, isOn, onText = "ON", offText = "OFF") {
  const element = document.getElementById(id);
  if (!element) {
    return;
  }

  element.textContent = isOn ? onText : offText;
  element.classList.remove("on", "off");
  element.classList.add(isOn ? "on" : "off");
}

function setStatusVariant(id, label, variant) {
  const element = document.getElementById(id);
  if (!element) {
    return;
  }

  element.textContent = label;
  element.classList.remove("on", "off", "tinyml-normal", "tinyml-warning", "tinyml-anomaly", "tinyml-idle");
  element.classList.add(variant);
}

function toProbability(value) {
  const numeric = Number(value);
  if (!Number.isFinite(numeric)) {
    return 0;
  }
  return Math.max(0, Math.min(1, numeric));
}

function formatProbability(value) {
  return `${(toProbability(value) * 100).toFixed(1)}%`;
}

function showSection(sectionId) {
  document.querySelectorAll(".section").forEach((section) => {
    section.style.display = section.id === sectionId ? "block" : "none";
  });

  document.querySelectorAll(".nav-item").forEach((button) => {
    button.classList.toggle("active", button.dataset.section === sectionId);
  });

  if (sectionId === "settings") {
    scanWifi();
    loadWifiStatus();
  }

  if (sectionId === "device" || sectionId === "remote") {
    loadDeviceState();
  }

  if (sectionId === "camera") {
    loadCameraConfig();
  }
}

async function fetchJson(path) {
  const response = await fetch(`${path}${path.includes("?") ? "&" : "?"}t=${Date.now()}`, {
    method: "GET",
    cache: "no-store"
  });

  if (!response.ok) {
    throw new Error(`HTTP ${response.status}`);
  }

  return response.json();
}

function formatLastSeen(ms) {
  if (!Number.isFinite(Number(ms)) || Number(ms) <= 0) {
    return "--";
  }

  const value = Number(ms);
  if (value < 1000) {
    return `${value} ms`;
  }

  return `${(value / 1000).toFixed(1)} s`;
}

function applyLocalState(data) {
  const doorOpen = String(data.door || "closed").toLowerCase() === "open";
  const fanOn = data.fan_on === true || String(data.fan || "OFF").toUpperCase() === "ON";
  const fanSpeed = Number.isFinite(Number(data.fan_speed)) ? Number(data.fan_speed) : 0;

  setBooleanBadge("doorState", doorOpen, "OPEN", "CLOSED");
  setBooleanBadge("doorOverviewBadge", doorOpen, "OPEN", "CLOSED");
  setBooleanBadge("fanState", fanOn);
  setBooleanBadge("fanOverviewBadge", fanOn);
  setText("fanSpeedValue", `${fanSpeed}%`);

  const fanSlider = document.getElementById("fanSpeedSlider");
  if (fanSlider) {
    fanSlider.value = String(fanSpeed);
  }
}

function applyRgbState(data) {
  const rgbOn = data.rgb_on === true || String(data.rgb_on).toLowerCase() === "true";
  const rgbHex = typeof data.rgb_hex === "string" && data.rgb_hex.trim() ? data.rgb_hex.trim().toUpperCase() : "#000000";

  setBooleanBadge("rgbState", rgbOn);
  setBooleanBadge("rgbOverviewBadge", rgbOn);

  const picker = document.getElementById("rgbPicker");
  const shouldSyncPicker = !rgbPickerDirty && picker && /^#[0-9A-F]{6}$/i.test(rgbHex);
  if (shouldSyncPicker) {
    picker.value = rgbHex;
    rgbPickerDraftHex = rgbHex;
  }

  const preview = document.getElementById("rgbPreview");
  if (preview) {
    const previewHex = rgbPickerDirty ? rgbPickerDraftHex : rgbHex;
    preview.style.background = previewHex;
    preview.style.boxShadow = rgbOn && !rgbPickerDirty ? `0 0 24px ${rgbHex}66` : `0 0 24px ${previewHex}66`;
  }

  setText("rgbHexValue", rgbPickerDirty ? rgbPickerDraftHex : rgbHex);
}

function applyRemoteState(data) {
  const remoteOnline = data.remote_online === true || String(data.remote_online).toLowerCase() === "true";
  const remoteDoorOpen = String(data.remote_door || "closed").toLowerCase() === "open";
  const remoteFanOn = data.remote_fan_on === true || String(data.remote_fan_on).toLowerCase() === "true";
  const remoteFanSpeed = Number.isFinite(Number(data.remote_fan_speed)) ? Number(data.remote_fan_speed) : 0;
  const remoteRgbOn = data.remote_rgb_on === true || String(data.remote_rgb_on).toLowerCase() === "true";
  const remoteRgbHex = typeof data.remote_rgb_hex === "string" && data.remote_rgb_hex.trim() ? data.remote_rgb_hex.trim().toUpperCase() : "#000000";

  setBooleanBadge("remoteDoorState", remoteDoorOpen, "OPEN", "CLOSED");
  setBooleanBadge("remoteFanState", remoteFanOn);
  setBooleanBadge("espNowLinkState", remoteOnline, "ONLINE", "OFFLINE");
  setBooleanBadge("remoteRgbState", remoteRgbOn);

  setText("remoteBoardTitle", data.remote_name || "Remote Board");
  setText("peerMacLabel", data.peer_mac || "--");
  setText("localMacLabel", data.local_mac || "--");
  setText("espNowStatusText", data.espnow_status || "--");
  setText("remoteTempValue", Number.isFinite(Number(data.remote_temp)) ? `${Number(data.remote_temp).toFixed(1)}°` : "--");
  setText("remoteHumiValue", Number.isFinite(Number(data.remote_hum)) ? `${Number(data.remote_hum).toFixed(1)}%` : "--");
  setText("remoteLastSeen", remoteOnline ? formatLastSeen(data.remote_last_seen_ms) : "offline");
  setText("remoteFanSpeedValue", `${remoteFanSpeed}%`);
  setText("remoteRgbHexValue", remoteRgbPickerDirty ? remoteRgbPickerDraftHex : remoteRgbHex);

  const peerInput = document.getElementById("peerMacInput");
  if (peerInput && data.peer_mac) {
    peerInput.value = data.peer_mac;
  }

  const remoteFanSlider = document.getElementById("remoteFanSpeedSlider");
  if (remoteFanSlider) {
    remoteFanSlider.value = String(remoteFanSpeed);
  }

  const remoteRgbPicker = document.getElementById("remoteRgbPicker");
  if (!remoteRgbPickerDirty && remoteRgbPicker && /^#[0-9A-F]{6}$/i.test(remoteRgbHex)) {
    remoteRgbPicker.value = remoteRgbHex;
    remoteRgbPickerDraftHex = remoteRgbHex;
  }

  const remotePreview = document.getElementById("remoteRgbPreview");
  if (remotePreview) {
    const previewHex = remoteRgbPickerDirty ? remoteRgbPickerDraftHex : remoteRgbHex;
    remotePreview.style.background = previewHex;
    remotePreview.style.boxShadow = remoteRgbOn && !remoteRgbPickerDirty ? `0 0 24px ${remoteRgbHex}66` : `0 0 24px ${previewHex}66`;
  }
}

function applyTinyMlState(data) {
  const ready = data.tinyml_ready === true || String(data.tinyml_ready).toLowerCase() === "true";
  const score = toProbability(data.tinyml_score);
  const probabilityNormal = toProbability(data.tinyml_prob_normal);
  const probabilityThreshold = toProbability(data.tinyml_prob_threshold);
  const probabilitySpike = toProbability(data.tinyml_prob_spike);
  const state = String(data.tinyml_state || (ready ? "NORMAL" : "IDLE")).toUpperCase();
  const description = data.tinyml_desc || "TinyML is waiting for sensor data.";
  const maxProbability = Math.max(probabilityNormal, probabilityThreshold, probabilitySpike, score);
  let severityText = "Waiting for data";
  let meterClass = "tinyml-normal";
  let badgeText = "IDLE";

  if (!ready || state === "IDLE") {
    badgeText = "IDLE";
    setStatusVariant("tinymlBadge", badgeText, "tinyml-idle");
    meterClass = "tinyml-idle";
  } else if (state === "ANOMALY") {
    badgeText = "SPIKE";
    setStatusVariant("tinymlBadge", badgeText, "tinyml-anomaly");
    severityText = "Sudden spike";
    meterClass = "tinyml-anomaly";
  } else if (state === "WARNING") {
    badgeText = "THRESHOLD";
    setStatusVariant("tinymlBadge", badgeText, "tinyml-warning");
    severityText = "Sustained threshold";
    meterClass = "tinyml-warning";
  } else {
    badgeText = "NORMAL";
    setStatusVariant("tinymlBadge", badgeText, "tinyml-normal");
    severityText = "In safe range";
  }

  setText("tinymlScore", ready ? score.toFixed(4) : "--");
  setText("tinymlTopConfidence", ready ? `${formatProbability(maxProbability)}` : "--");
  setText("tinymlDesc", description);
  setText("tinymlSeverityText", severityText);

  const normalBar = document.getElementById("tinymlProbNormalBar");
  if (normalBar) {
    normalBar.style.width = ready ? `${probabilityNormal * 100}%` : "0%";
  }

  const thresholdBar = document.getElementById("tinymlProbThresholdBar");
  if (thresholdBar) {
    thresholdBar.style.width = ready ? `${probabilityThreshold * 100}%` : "0%";
  }

  const spikeBar = document.getElementById("tinymlProbSpikeBar");
  if (spikeBar) {
    spikeBar.style.width = ready ? `${probabilitySpike * 100}%` : "0%";
  }

  setText("tinymlProbNormalValue", ready ? formatProbability(probabilityNormal) : "--");
  setText("tinymlProbThresholdValue", ready ? formatProbability(probabilityThreshold) : "--");
  setText("tinymlProbSpikeValue", ready ? formatProbability(probabilitySpike) : "--");

  const meter = document.getElementById("tinymlMeterFill");
  if (meter) {
    meter.style.width = `${ready ? Math.max(0, Math.min(100, score * 100)) : 0}%`;
    meter.classList.remove("tinyml-normal", "tinyml-warning", "tinyml-anomaly", "tinyml-idle");
    meter.classList.add(meterClass);
  }

  const pointer = document.getElementById("tinymlPointer");
  if (pointer) {
    pointer.style.left = `${ready ? Math.max(0, Math.min(100, score * 100)) : 0}%`;
    pointer.style.opacity = ready ? "1" : "0.45";
  }
}

function applyMnistState(data) {
  const ready = data.mnist_ready === true || String(data.mnist_ready).toLowerCase() === "true";
  const digit = Number.isFinite(Number(data.mnist_digit)) && Number(data.mnist_digit) >= 0 ? Number(data.mnist_digit) : null;
  const confidence = Number.isFinite(Number(data.mnist_confidence)) ? Number(data.mnist_confidence) : 0;
  const status = data.mnist_status || "The ESP32 is waiting for camera configuration.";
  const source = data.camera_host || "--";

  if (ready) {
    setStatusVariant("mnistBadge", "READY", "tinyml-normal");
  } else {
    setStatusVariant("mnistBadge", "WAIT", "off");
  }

  setText("mnistDigit", digit === null ? "--" : String(digit));
  setText("mnistConfidence", ready ? confidence.toFixed(3) : "--");
  setText("mnistSource", source);
  setText("mnistStatusText", status);
}

function applyUnifiedState(data) {
  setText("deviceIp", window.location.host);
  applyLocalState(data);
  applyRgbState(data);
  applyRemoteState(data);
  applyTinyMlState(data);
  applyMnistState(data);
}

async function loadSensors() {
  try {
    const data = await fetchJson("/sensors");
    setText("temp", Number.isFinite(Number(data.temp)) ? `${Number(data.temp).toFixed(1)}°` : "--");
    setText("hum", Number.isFinite(Number(data.hum)) ? `${Number(data.hum).toFixed(1)}%` : "--");
    setText("wifiStatus", "Running");
    applyUnifiedState(data);
  } catch (error) {
    console.error("Failed to fetch sensor data:", error);
    setText("temp", "--");
    setText("hum", "--");
    setText("wifiStatus", "Disconnected");
  }
}

async function loadDeviceState() {
  try {
    const data = await fetchJson("/state");
    applyUnifiedState(data);
  } catch (error) {
    console.error("Failed to fetch device state:", error);
  }
}

async function controlDoor(state) {
  try {
    await fetchJson(`/door?state=${state}`);
    await loadDeviceState();
  } catch (error) {
    console.error("Door control error:", error);
    alert("Door control failed");
  }
}

async function setFan(state) {
  try {
    await fetchJson(`/fan?state=${state}`);
    await loadDeviceState();
  } catch (error) {
    console.error("Fan control error:", error);
    alert("Unable to control the fan");
  }
}

async function setFanSpeed(value) {
  try {
    await fetchJson(`/fan?speed=${value}`);
    await loadDeviceState();
  } catch (error) {
    console.error("Fan speed error:", error);
    alert("Unable to adjust fan speed");
  }
}

async function applyRgbColor() {
  const picker = document.getElementById("rgbPicker");
  const hexColor = picker?.value?.trim() || "#000000";

  try {
    await fetchJson(`/rgb?hex=${encodeURIComponent(hexColor)}`);
    rgbPickerDirty = false;
    rgbPickerDraftHex = hexColor.toUpperCase();
    await loadDeviceState();
  } catch (error) {
    console.error("RGB control error:", error);
    alert("Unable to control the peripheral NeoPixel");
  }
}

async function turnOffRgb() {
  try {
    await fetchJson("/rgb?hex=000000");
    rgbPickerDirty = false;
    rgbPickerDraftHex = "#000000";
    await loadDeviceState();
  } catch (error) {
    console.error("RGB off error:", error);
    alert("Unable to turn off the peripheral NeoPixel");
  }
}

async function controlRemoteDoor(state) {
  try {
    const data = await fetchJson(`/remote/door?state=${state}`);
    applyRemoteState(data);
    setTimeout(loadDeviceState, 1200);
  } catch (error) {
    console.error("Remote door error:", error);
    alert("Unable to send the door command to the peer board");
  }
}

async function setRemoteFan(state) {
  try {
    const data = await fetchJson(`/remote/fan?state=${state}`);
    applyRemoteState(data);
    setTimeout(loadDeviceState, 1200);
  } catch (error) {
    console.error("Remote fan error:", error);
    alert("Unable to send the fan command to the peer board");
  }
}

async function setRemoteFanSpeed(value) {
  try {
    const data = await fetchJson(`/remote/fan?speed=${value}`);
    applyRemoteState(data);
    setTimeout(loadDeviceState, 1200);
  } catch (error) {
    console.error("Remote fan speed error:", error);
    alert("Unable to adjust the remote fan speed");
  }
}

async function applyRemoteRgbColor() {
  const picker = document.getElementById("remoteRgbPicker");
  const hexColor = picker?.value?.trim() || "#000000";

  try {
    const data = await fetchJson(`/remote/rgb?hex=${encodeURIComponent(hexColor)}`);
    remoteRgbPickerDirty = false;
    remoteRgbPickerDraftHex = hexColor.toUpperCase();
    applyRemoteState(data);
    setTimeout(loadDeviceState, 1200);
  } catch (error) {
    console.error("Remote RGB control error:", error);
    alert("Unable to send the RGB color to the peer board");
  }
}

async function turnOffRemoteRgb() {
  try {
    const data = await fetchJson("/remote/rgb?hex=000000");
    remoteRgbPickerDirty = false;
    remoteRgbPickerDraftHex = "#000000";
    applyRemoteState(data);
    setTimeout(loadDeviceState, 1200);
  } catch (error) {
    console.error("Remote RGB off error:", error);
    alert("Unable to turn off the peer board RGB");
  }
}

async function connectWifi(event) {
  if (event) {
    event.preventDefault();
  }

  const ssid = document.getElementById("ssid")?.value?.trim() || "";
  const password = document.getElementById("password")?.value || "";
  const submitButton = document.querySelector('#settingsForm button[type="submit"]');

  if (!ssid) {
    alert("Please enter the SSID");
    return;
  }

  try {
    if (submitButton) {
      submitButton.disabled = true;
      submitButton.textContent = "Connecting...";
    }

    const data = await fetchJson(`/connect?ssid=${encodeURIComponent(ssid)}&pass=${encodeURIComponent(password)}`);
    if (data.ok !== true) {
      throw new Error(data.error || "Invalid response");
    }

    setText("wifiStatus", "Connecting WiFi...");
    waitForWifiConnection();
  } catch (error) {
    console.error("WiFi connection error:", error);
    setText("wifiStatus", "Configuration failed");
    alert("Unable to send WiFi configuration");
  } finally {
    if (submitButton) {
      submitButton.disabled = false;
      submitButton.textContent = "Connect WiFi";
    }
  }
}

async function waitForWifiConnection() {
  for (let attempt = 0; attempt < 20; attempt += 1) {
    try {
      const data = await fetchJson("/wifi_status");

      if (data.connecting) {
        setText("wifiStatus", "Connecting WiFi...");
      }

      if (data.connected) {
        setText("wifiStatus", `Connected: ${data.ssid || ""}`);
        if (data.sta_ip) {
          alert(`WiFi connected successfully\nDevice IP: ${data.sta_ip}`);
        }
        return;
      }

      if (!data.connecting && !data.connected && data.message) {
        setText("wifiStatus", data.message);
      }
    } catch (error) {
      console.error("wifi_status error:", error);
    }

    await new Promise((resolve) => setTimeout(resolve, 1500));
  }

  setText("wifiStatus", "WiFi connection failed or timed out");
  alert("The device could not connect to WiFi. Check the SSID and password.");
}

async function loadWifiStatus() {
  try {
    const data = await fetchJson("/wifi_status");
    if (data.connected) {
      setText("wifiStatus", `WiFi: ${data.ssid || "STA"} | IP: ${data.sta_ip || "--"}`);
    } else if (data.connecting) {
      setText("wifiStatus", `Connecting WiFi ${data.ssid ? `(${data.ssid})` : ""}...`);
    } else {
      setText("wifiStatus", data.message || "Not connected");
    }

    const ssidInput = document.getElementById("ssid");
    if (ssidInput && data.ssid && !ssidInput.value) {
      ssidInput.value = data.ssid;
    }
  } catch (error) {
    console.error("Failed to fetch WiFi status:", error);
  }
}

async function scanWifi() {
  try {
    const data = await fetchJson("/scan_wifi");
    const list = document.getElementById("wifiList");
    if (!list) {
      return;
    }

    list.innerHTML = "";
    if (!Array.isArray(data) || data.length === 0) {
      list.innerHTML = `<div class="wifi-item"><strong>No WiFi networks found</strong><span>Try scanning again</span></div>`;
      return;
    }

    data.sort((left, right) => right.rssi - left.rssi);
    data.forEach((network) => {
      const item = document.createElement("div");
      item.className = "wifi-item";
      item.innerHTML = `
        <strong>${network.ssid}</strong>
        <span>${network.rssi} dBm ${network.secure ? "LOCK" : "OPEN"}</span>
      `;

      item.addEventListener("click", () => {
        const ssidInput = document.getElementById("ssid");
        if (ssidInput) {
          ssidInput.value = network.ssid;
        }
      });

      list.appendChild(item);
    });
  } catch (error) {
    console.error("WiFi scan error:", error);
    const list = document.getElementById("wifiList");
    if (list) {
      list.innerHTML = `<div class="wifi-item"><strong>Unable to scan WiFi</strong><span>Check the connection again</span></div>`;
    }
  }
}

async function loadCameraConfig() {
  try {
    const data = await fetchJson("/camera_config");
    const input = document.getElementById("pcIpInput");
    if (input && data.camera_host) {
      input.value = data.camera_host;
    }
    applyMnistState(data);
  } catch (error) {
    console.error("Failed to fetch camera configuration:", error);
  }
}

async function savePcIp() {
  const input = document.getElementById("pcIpInput");
  const ip = input ? input.value.trim() : "";
  if (!ip) {
    alert("Please enter the PC IP");
    return false;
  }

  try {
    const data = await fetchJson(`/camera_config?host=${encodeURIComponent(ip)}`);
    localStorage.setItem("pc_ip", ip);
    setText("camStatus", `ESP32 saved host: ${ip}`);
    applyMnistState(data);
    return true;
  } catch (error) {
    console.error("Failed to save camera host:", error);
    alert("Unable to send the camera host to the ESP32");
    return false;
  }
}

async function savePeerMac() {
  const input = document.getElementById("peerMacInput");
  const peerMac = input ? input.value.trim() : "";

  try {
    const data = await fetchJson(`/espnow_config?peer=${encodeURIComponent(peerMac)}`);
    applyRemoteState(data);
    return true;
  } catch (error) {
    console.error("Failed to save peer MAC:", error);
    alert("Unable to send the peer MAC to the firmware");
    return false;
  }
}

async function connectCamera() {
  const ip = getCameraHost();

  if (!ip) {
    alert("No PC IP configured");
    return;
  }

  const saved = await savePcIp();
  if (!saved) {
    return;
  }

  const image = document.getElementById("cameraView");
  if (image) {
    image.src = `${getCameraBaseUrl()}/video_feed?t=${Date.now()}`;
  }

  setText("camStatus", `Connecting stream: http://${ip}/video_feed`);
}

async function setCameraSource(sourceName) {
  const baseUrl = getCameraBaseUrl();
  if (!baseUrl) {
    alert("No PC IP configured");
    return false;
  }

  const response = await fetch(`${baseUrl}/source?name=${encodeURIComponent(sourceName)}`, {
    method: "POST",
    cache: "no-store"
  });

  if (!response.ok) {
    throw new Error(`HTTP ${response.status}`);
  }

  return response.json();
}

async function refreshCapturedPreview() {
  const image = document.getElementById("cameraView");
  if (image) {
    image.src = `${getCameraBaseUrl()}/current_frame?t=${Date.now()}`;
  }
}

async function useCameraSource() {
  try {
    const saved = await savePcIp();
    if (!saved) {
      return;
    }

    await setCameraSource("camera");
    await connectCamera();
    setText("camStatus", `Using live webcam: ${getCameraBaseUrl()}/video_feed`);
  } catch (error) {
    console.error("Failed to switch to webcam:", error);
    alert("Unable to switch to the webcam source");
  }
}

async function uploadTestImage() {
  const baseUrl = getCameraBaseUrl();
  const input = document.getElementById("imageUploadInput");
  const file = input?.files?.[0];

  if (!baseUrl) {
    alert("No PC IP configured");
    return;
  }

  if (!file) {
    alert("Please choose an image file");
    return;
  }

  try {
    const saved = await savePcIp();
    if (!saved) {
      return;
    }

    const formData = new FormData();
    formData.append("image", file);

    const response = await fetch(`${baseUrl}/upload_image`, {
      method: "POST",
      body: formData,
      cache: "no-store"
    });

    const payload = await parseJsonSafe(response);
    if (!response.ok) {
      throw new Error(payload.error || `HTTP ${response.status}`);
    }

    await refreshCapturedPreview();

    setText("camStatus", `Uploaded test image: ${file.name}`);
  } catch (error) {
    console.error("Failed to upload test image:", error);
    alert(`Unable to upload the test image to the camera server\n${error.message || ""}`.trim());
  }
}

async function captureCurrentFrame() {
  const baseUrl = getCameraBaseUrl();
  if (!baseUrl) {
    alert("No PC IP configured");
    return;
  }

  try {
    const saved = await savePcIp();
    if (!saved) {
      return;
    }

    const response = await fetch(`${baseUrl}/capture_frame`, {
      method: "POST",
      cache: "no-store"
    });

    const payload = await parseJsonSafe(response);
    if (!response.ok) {
      throw new Error(payload.error || `HTTP ${response.status}`);
    }

    await refreshCapturedPreview();
    setText("camStatus", "Captured the current webcam frame for detection");
  } catch (error) {
    console.error("Failed to capture webcam frame:", error);
    alert(`Unable to capture the current frame\n${error.message || ""}`.trim());
  }
}

function disconnectCamera() {
  const image = document.getElementById("cameraView");
  if (image) {
    image.src = "";
  }
  setText("camStatus", "Camera stream disconnected");
}

function bindEvents() {
  document.querySelectorAll(".nav-item").forEach((button) => {
    button.addEventListener("click", () => showSection(button.dataset.section));
  });

  document.getElementById("refreshButton")?.addEventListener("click", loadSensors);
  document.getElementById("openDoor")?.addEventListener("click", () => controlDoor("open"));
  document.getElementById("closeDoor")?.addEventListener("click", () => controlDoor("close"));
  document.getElementById("fanOnButton")?.addEventListener("click", () => setFan("on"));
  document.getElementById("fanOffButton")?.addEventListener("click", () => setFan("off"));
  document.getElementById("applyRgbButton")?.addEventListener("click", applyRgbColor);
  document.getElementById("turnOffRgbButton")?.addEventListener("click", turnOffRgb);
  document.getElementById("scanWifiButton")?.addEventListener("click", scanWifi);
  document.getElementById("savePcIpButton")?.addEventListener("click", savePcIp);
  document.getElementById("connectCameraButton")?.addEventListener("click", connectCamera);
  document.getElementById("disconnectCameraButton")?.addEventListener("click", disconnectCamera);
  document.getElementById("useCameraSourceButton")?.addEventListener("click", useCameraSource);
  document.getElementById("uploadImageButton")?.addEventListener("click", uploadTestImage);
  document.getElementById("captureFrameButton")?.addEventListener("click", captureCurrentFrame);
  document.getElementById("settingsForm")?.addEventListener("submit", connectWifi);
  document.getElementById("fanSpeedSlider")?.addEventListener("input", (event) => {
    setText("fanSpeedValue", `${event.target.value}%`);
  });
  document.getElementById("fanSpeedSlider")?.addEventListener("change", (event) => {
    setFanSpeed(event.target.value);
  });
  document.getElementById("rgbPicker")?.addEventListener("input", (event) => {
    const hexColor = String(event.target.value || "#000000").toUpperCase();
    rgbPickerDirty = true;
    rgbPickerDraftHex = hexColor;
    setText("rgbHexValue", hexColor);
    const preview = document.getElementById("rgbPreview");
    if (preview) {
      preview.style.background = hexColor;
      preview.style.boxShadow = `0 0 24px ${hexColor}66`;
    }
  });

  document.getElementById("savePeerMacButton")?.addEventListener("click", savePeerMac);
  document.getElementById("refreshEspNowButton")?.addEventListener("click", loadDeviceState);
  document.getElementById("openRemoteDoor")?.addEventListener("click", () => controlRemoteDoor("open"));
  document.getElementById("closeRemoteDoor")?.addEventListener("click", () => controlRemoteDoor("close"));
  document.getElementById("remoteFanOnButton")?.addEventListener("click", () => setRemoteFan("on"));
  document.getElementById("remoteFanOffButton")?.addEventListener("click", () => setRemoteFan("off"));
  document.getElementById("applyRemoteRgbButton")?.addEventListener("click", applyRemoteRgbColor);
  document.getElementById("turnOffRemoteRgbButton")?.addEventListener("click", turnOffRemoteRgb);
  document.getElementById("remoteFanSpeedSlider")?.addEventListener("input", (event) => {
    setText("remoteFanSpeedValue", `${event.target.value}%`);
  });
  document.getElementById("remoteFanSpeedSlider")?.addEventListener("change", (event) => {
    setRemoteFanSpeed(event.target.value);
  });
  document.getElementById("remoteRgbPicker")?.addEventListener("input", (event) => {
    const hexColor = String(event.target.value || "#000000").toUpperCase();
    remoteRgbPickerDirty = true;
    remoteRgbPickerDraftHex = hexColor;
    setText("remoteRgbHexValue", hexColor);
    const preview = document.getElementById("remoteRgbPreview");
    if (preview) {
      preview.style.background = hexColor;
      preview.style.boxShadow = `0 0 24px ${hexColor}66`;
    }
  });
}

function restoreSavedCameraHost() {
  const saved = localStorage.getItem("pc_ip");
  if (!saved) {
    return;
  }

  const input = document.getElementById("pcIpInput");
  if (input) {
    input.value = saved;
  }
}

window.addEventListener("DOMContentLoaded", () => {
  bindEvents();
  restoreSavedCameraHost();
  showSection("dashboard");

  loadSensors();
  loadDeviceState();
  loadWifiStatus();
  scanWifi();
  loadCameraConfig();

  setInterval(loadSensors, POLL_SENSORS_MS);
  setInterval(loadDeviceState, POLL_STATE_MS);
  setInterval(loadWifiStatus, POLL_WIFI_MS);
  setInterval(loadCameraConfig, POLL_CAMERA_MS);
});
