const POLL_SENSORS_MS = 2000;
const POLL_STATE_MS = 3000;
const POLL_WIFI_MS = 3000;
const POLL_CAMERA_MS = 4000;

function getCameraHost() {
  return document.getElementById("pcIpInput")?.value?.trim() || localStorage.getItem("pc_ip") || "";
}

function getCameraBaseUrl() {
  const host = getCameraHost();
  return host ? `http://${host}` : "";
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
  setText("rgbHexValue", rgbHex);

  const picker = document.getElementById("rgbPicker");
  if (picker && /^#[0-9A-F]{6}$/i.test(rgbHex)) {
    picker.value = rgbHex;
  }

  const preview = document.getElementById("rgbPreview");
  if (preview) {
    preview.style.background = rgbHex;
    preview.style.boxShadow = rgbOn ? `0 0 24px ${rgbHex}66` : "inset 0 1px 0 rgba(255,255,255,0.1)";
  }
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
  setText("remoteRgbHexValue", remoteRgbHex);

  const peerInput = document.getElementById("peerMacInput");
  if (peerInput && data.peer_mac) {
    peerInput.value = data.peer_mac;
  }

  const remoteFanSlider = document.getElementById("remoteFanSpeedSlider");
  if (remoteFanSlider) {
    remoteFanSlider.value = String(remoteFanSpeed);
  }

  const remoteRgbPicker = document.getElementById("remoteRgbPicker");
  if (remoteRgbPicker && /^#[0-9A-F]{6}$/i.test(remoteRgbHex)) {
    remoteRgbPicker.value = remoteRgbHex;
  }

  const remotePreview = document.getElementById("remoteRgbPreview");
  if (remotePreview) {
    remotePreview.style.background = remoteRgbHex;
    remotePreview.style.boxShadow = remoteRgbOn ? `0 0 24px ${remoteRgbHex}66` : "inset 0 1px 0 rgba(255,255,255,0.1)";
  }
}

function applyTinyMlState(data) {
  const ready = data.tinyml_ready === true || String(data.tinyml_ready).toLowerCase() === "true";
  const score = Number.isFinite(Number(data.tinyml_score)) ? Number(data.tinyml_score) : 0;
  const state = String(data.tinyml_state || (ready ? "NORMAL" : "IDLE")).toUpperCase();
  const description = data.tinyml_desc || "TinyML dang cho du lieu cam bien.";
  let severityText = "Dang doi du lieu";
  let meterClass = "tinyml-normal";

  if (!ready || state === "IDLE") {
    setStatusVariant("tinymlBadge", "IDLE", "tinyml-idle");
    meterClass = "tinyml-idle";
  } else if (state === "ANOMALY") {
    setStatusVariant("tinymlBadge", "ANOMALY", "tinyml-anomaly");
    severityText = "Bat thuong cao";
    meterClass = "tinyml-anomaly";
  } else if (state === "WARNING") {
    setStatusVariant("tinymlBadge", "WARNING", "tinyml-warning");
    severityText = "Can theo doi";
    meterClass = "tinyml-warning";
  } else {
    setStatusVariant("tinymlBadge", "NORMAL", "tinyml-normal");
    severityText = "Trong vung an toan";
  }

  setText("tinymlScore", ready ? score.toFixed(4) : "--");
  setText("tinymlDesc", description);
  setText("tinymlSeverityText", severityText);

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
  const status = data.mnist_status || "ESP32 dang cho cau hinh camera.";
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
    setText("wifiStatus", "Dang hoat dong");
    applyUnifiedState(data);
  } catch (error) {
    console.error("Khong lay duoc du lieu cam bien:", error);
    setText("temp", "--");
    setText("hum", "--");
    setText("wifiStatus", "Mat ket noi");
  }
}

async function loadDeviceState() {
  try {
    const data = await fetchJson("/state");
    applyUnifiedState(data);
  } catch (error) {
    console.error("Khong lay duoc trang thai thiet bi:", error);
  }
}

async function controlDoor(state) {
  try {
    await fetchJson(`/door?state=${state}`);
    await loadDeviceState();
  } catch (error) {
    console.error("Door control error:", error);
    alert("Loi dieu khien cua");
  }
}

async function setFan(state) {
  try {
    await fetchJson(`/fan?state=${state}`);
    await loadDeviceState();
  } catch (error) {
    console.error("Fan control error:", error);
    alert("Khong dieu khien duoc quat");
  }
}

async function setFanSpeed(value) {
  try {
    await fetchJson(`/fan?speed=${value}`);
    await loadDeviceState();
  } catch (error) {
    console.error("Fan speed error:", error);
    alert("Khong chinh duoc toc do quat");
  }
}

async function applyRgbColor() {
  const picker = document.getElementById("rgbPicker");
  const hexColor = picker?.value?.trim() || "#000000";

  try {
    await fetchJson(`/rgb?hex=${encodeURIComponent(hexColor)}`);
    await loadDeviceState();
  } catch (error) {
    console.error("RGB control error:", error);
    alert("Khong dieu khien duoc NeoPixel ngoai vi");
  }
}

async function turnOffRgb() {
  try {
    await fetchJson("/rgb?hex=000000");
    await loadDeviceState();
  } catch (error) {
    console.error("RGB off error:", error);
    alert("Khong tat duoc NeoPixel ngoai vi");
  }
}

async function controlRemoteDoor(state) {
  try {
    await fetchJson(`/remote/door?state=${state}`);
    setTimeout(loadDeviceState, 400);
  } catch (error) {
    console.error("Remote door error:", error);
    alert("Khong gui duoc lenh cua toi board giao tiep");
  }
}

async function setRemoteFan(state) {
  try {
    await fetchJson(`/remote/fan?state=${state}`);
    setTimeout(loadDeviceState, 400);
  } catch (error) {
    console.error("Remote fan error:", error);
    alert("Khong gui duoc lenh quat toi board giao tiep");
  }
}

async function setRemoteFanSpeed(value) {
  try {
    await fetchJson(`/remote/fan?speed=${value}`);
    setTimeout(loadDeviceState, 400);
  } catch (error) {
    console.error("Remote fan speed error:", error);
    alert("Khong chinh duoc toc do quat remote");
  }
}

async function applyRemoteRgbColor() {
  const picker = document.getElementById("remoteRgbPicker");
  const hexColor = picker?.value?.trim() || "#000000";

  try {
    await fetchJson(`/remote/rgb?hex=${encodeURIComponent(hexColor)}`);
    setTimeout(loadDeviceState, 400);
  } catch (error) {
    console.error("Remote RGB control error:", error);
    alert("Khong gui duoc mau RGB toi board giao tiep");
  }
}

async function turnOffRemoteRgb() {
  try {
    await fetchJson("/remote/rgb?hex=000000");
    setTimeout(loadDeviceState, 400);
  } catch (error) {
    console.error("Remote RGB off error:", error);
    alert("Khong tat duoc RGB cua board giao tiep");
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
    alert("Vui long nhap SSID");
    return;
  }

  try {
    if (submitButton) {
      submitButton.disabled = true;
      submitButton.textContent = "Dang ket noi...";
    }

    const data = await fetchJson(`/connect?ssid=${encodeURIComponent(ssid)}&pass=${encodeURIComponent(password)}`);
    if (data.ok !== true) {
      throw new Error(data.error || "Phan hoi khong hop le");
    }

    setText("wifiStatus", "Dang ket noi WiFi...");
    waitForWifiConnection();
  } catch (error) {
    console.error("Ket noi WiFi loi:", error);
    setText("wifiStatus", "Gui cau hinh that bai");
    alert("Khong gui duoc cau hinh WiFi");
  } finally {
    if (submitButton) {
      submitButton.disabled = false;
      submitButton.textContent = "Ket noi WiFi";
    }
  }
}

async function waitForWifiConnection() {
  for (let attempt = 0; attempt < 20; attempt += 1) {
    try {
      const data = await fetchJson("/wifi_status");

      if (data.connecting) {
        setText("wifiStatus", "Dang ket noi WiFi...");
      }

      if (data.connected) {
        setText("wifiStatus", `Da ket noi: ${data.ssid || ""}`);
        if (data.sta_ip) {
          alert(`Ket noi WiFi thanh cong\nIP moi cua thiet bi: ${data.sta_ip}`);
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

  setText("wifiStatus", "Ket noi WiFi that bai hoac timeout");
  alert("Thiet bi chua ket noi duoc WiFi. Hay kiem tra SSID va mat khau.");
}

async function loadWifiStatus() {
  try {
    const data = await fetchJson("/wifi_status");
    if (data.connected) {
      setText("wifiStatus", `WiFi: ${data.ssid || "STA"} | IP: ${data.sta_ip || "--"}`);
    } else if (data.connecting) {
      setText("wifiStatus", `Dang ket noi WiFi ${data.ssid ? `(${data.ssid})` : ""}...`);
    } else {
      setText("wifiStatus", data.message || "Chua ket noi");
    }

    const ssidInput = document.getElementById("ssid");
    if (ssidInput && data.ssid && !ssidInput.value) {
      ssidInput.value = data.ssid;
    }
  } catch (error) {
    console.error("Khong lay duoc trang thai WiFi:", error);
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
      list.innerHTML = `<div class="wifi-item"><strong>Khong tim thay mang WiFi</strong><span>Thu quet lai</span></div>`;
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
    console.error("Scan WiFi loi:", error);
    const list = document.getElementById("wifiList");
    if (list) {
      list.innerHTML = `<div class="wifi-item"><strong>Khong quet duoc WiFi</strong><span>Kiem tra lai ket noi</span></div>`;
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
    console.error("Khong lay duoc cau hinh camera:", error);
  }
}

async function savePcIp() {
  const input = document.getElementById("pcIpInput");
  const ip = input ? input.value.trim() : "";
  if (!ip) {
    alert("Vui long nhap IP PC");
    return false;
  }

  try {
    const data = await fetchJson(`/camera_config?host=${encodeURIComponent(ip)}`);
    localStorage.setItem("pc_ip", ip);
    setText("camStatus", `ESP32 da luu host: ${ip}`);
    applyMnistState(data);
    return true;
  } catch (error) {
    console.error("Khong luu duoc camera host:", error);
    alert("Khong gui duoc camera host toi ESP32");
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
    console.error("Khong luu duoc peer MAC:", error);
    alert("Khong gui duoc peer MAC toi firmware");
    return false;
  }
}

async function connectCamera() {
  const ip = getCameraHost();

  if (!ip) {
    alert("Chua co IP PC");
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

  setText("camStatus", `Dang ket noi stream: http://${ip}/video_feed`);
}

async function setCameraSource(sourceName) {
  const baseUrl = getCameraBaseUrl();
  if (!baseUrl) {
    alert("Chua co IP PC");
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

async function useCameraSource() {
  try {
    const saved = await savePcIp();
    if (!saved) {
      return;
    }

    await setCameraSource("camera");
    await connectCamera();
    setText("camStatus", `Dang dung webcam live: ${getCameraBaseUrl()}/video_feed`);
  } catch (error) {
    console.error("Khong chuyen duoc sang webcam:", error);
    alert("Khong chuyen duoc sang nguon webcam");
  }
}

async function uploadTestImage() {
  const baseUrl = getCameraBaseUrl();
  const input = document.getElementById("imageUploadInput");
  const file = input?.files?.[0];

  if (!baseUrl) {
    alert("Chua co IP PC");
    return;
  }

  if (!file) {
    alert("Hay chon mot file anh");
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

    if (!response.ok) {
      throw new Error(`HTTP ${response.status}`);
    }

    const image = document.getElementById("cameraView");
    if (image) {
      image.src = `${baseUrl}/current_frame?t=${Date.now()}`;
    }

    setText("camStatus", `Da tai anh test: ${file.name}`);
  } catch (error) {
    console.error("Khong tai duoc anh test:", error);
    alert("Khong tai duoc anh test len camera server");
  }
}

function disconnectCamera() {
  const image = document.getElementById("cameraView");
  if (image) {
    image.src = "";
  }
  setText("camStatus", "Da ngat camera stream");
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
  document.getElementById("settingsForm")?.addEventListener("submit", connectWifi);
  document.getElementById("fanSpeedSlider")?.addEventListener("input", (event) => {
    setText("fanSpeedValue", `${event.target.value}%`);
  });
  document.getElementById("fanSpeedSlider")?.addEventListener("change", (event) => {
    setFanSpeed(event.target.value);
  });
  document.getElementById("rgbPicker")?.addEventListener("input", (event) => {
    const hexColor = String(event.target.value || "#000000").toUpperCase();
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
