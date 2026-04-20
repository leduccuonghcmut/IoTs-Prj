const POLL_SENSORS_MS = 2000;
const POLL_STATE_MS = 3000;
const POLL_WIFI_MS = 3000;
const POLL_CAMERA_MS = 4000;

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

  if (sectionId === "device") {
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

function applyDeviceState(data) {
  const led1On = data.led1 === true || String(data.led1 || "OFF").toUpperCase() === "ON";
  const led2On = data.led2 === true || String(data.led2 || "OFF").toUpperCase() === "ON";
  const doorOpen = String(data.door || "closed").toLowerCase() === "open";
  const fanOn = data.fan_on === true || String(data.fan || "OFF").toUpperCase() === "ON";
  const fanSpeed = Number.isFinite(Number(data.fan_speed)) ? Number(data.fan_speed) : 0;

  setBooleanBadge("led1", led1On);
  setBooleanBadge("led2", led2On);
  setBooleanBadge("led1Badge", led1On);
  setBooleanBadge("led2Badge", led2On);

  setBooleanBadge("doorState", doorOpen, "OPEN", "CLOSED");
  setBooleanBadge("doorOverviewBadge", doorOpen, "OPEN", "CLOSED");

  setBooleanBadge("fanState", fanOn);
  setBooleanBadge("fanOverviewBadge", fanOn);
  setText("fanSpeedValue", `${fanSpeed}%`);

  const slider = document.getElementById("fanSpeedSlider");
  if (slider) {
    slider.value = String(fanSpeed);
  }
}

function applyTinyMlState(data) {
  const ready = data.tinyml_ready === true || String(data.tinyml_ready).toLowerCase() === "true";
  const score = Number.isFinite(Number(data.tinyml_score)) ? Number(data.tinyml_score) : 0;
  const state = String(data.tinyml_state || (ready ? "NORMAL" : "IDLE")).toUpperCase();
  const description = data.tinyml_desc || "TinyML dang cho du lieu cam bien.";

  if (!ready || state === "IDLE") {
    setStatusVariant("tinymlBadge", "IDLE", "tinyml-idle");
  } else if (state === "ANOMALY") {
    setStatusVariant("tinymlBadge", "ANOMALY", "tinyml-anomaly");
  } else if (state === "WARNING") {
    setStatusVariant("tinymlBadge", "WARNING", "tinyml-warning");
  } else {
    setStatusVariant("tinymlBadge", "NORMAL", "tinyml-normal");
  }

  setText("tinymlScore", ready ? score.toFixed(4) : "--");
  setText("tinymlDesc", description);

  const meter = document.getElementById("tinymlMeterFill");
  if (meter) {
    meter.style.width = `${ready ? Math.max(0, Math.min(100, score * 100)) : 0}%`;
    meter.classList.remove("tinyml-normal", "tinyml-warning", "tinyml-anomaly");

    if (state === "ANOMALY") {
      meter.classList.add("tinyml-anomaly");
    } else if (state === "WARNING") {
      meter.classList.add("tinyml-warning");
    } else {
      meter.classList.add("tinyml-normal");
    }
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

async function loadSensors() {
  try {
    const data = await fetchJson("/sensors");
    setText("temp", Number.isFinite(Number(data.temp)) ? `${Number(data.temp).toFixed(1)}°` : "--");
    setText("hum", Number.isFinite(Number(data.hum)) ? `${Number(data.hum).toFixed(1)}%` : "--");
    setText("deviceIp", window.location.host);
    setText("wifiStatus", "Dang hoat dong");

    applyDeviceState(data);
    applyTinyMlState(data);
    applyMnistState(data);
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
    applyDeviceState(data);
    applyTinyMlState(data);
    applyMnistState(data);
  } catch (error) {
    console.error("Khong lay duoc trang thai thiet bi:", error);
  }
}

async function setDevice(deviceNumber) {
  try {
    await fetchJson(`/toggle?led=${deviceNumber}`);
    await loadDeviceState();
  } catch (error) {
    console.error("Khong dieu khien duoc thiet bi:", error);
    alert("Dieu khien that bai");
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
      setText("wifiStatus", `Da ket noi: ${data.ssid || ""}`);
    } else if (data.connecting) {
      setText("wifiStatus", "Dang ket noi WiFi...");
    } else {
      setText("wifiStatus", data.message || "Chua ket noi");
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

async function connectCamera() {
  let ip = document.getElementById("pcIpInput")?.value?.trim() || "";
  if (!ip) {
    ip = localStorage.getItem("pc_ip") || "";
  }

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
    image.src = `http://${ip}/video_feed`;
  }

  setText("camStatus", `Dang ket noi stream: http://${ip}/video_feed`);
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
  document.getElementById("toggleDevice1")?.addEventListener("click", () => setDevice(1));
  document.getElementById("toggleDevice2")?.addEventListener("click", () => setDevice(2));
  document.getElementById("openDoor")?.addEventListener("click", () => controlDoor("open"));
  document.getElementById("closeDoor")?.addEventListener("click", () => controlDoor("close"));
  document.getElementById("fanOnButton")?.addEventListener("click", () => setFan("on"));
  document.getElementById("fanOffButton")?.addEventListener("click", () => setFan("off"));
  document.getElementById("scanWifiButton")?.addEventListener("click", scanWifi);
  document.getElementById("savePcIpButton")?.addEventListener("click", savePcIp);
  document.getElementById("connectCameraButton")?.addEventListener("click", connectCamera);
  document.getElementById("disconnectCameraButton")?.addEventListener("click", disconnectCamera);
  document.getElementById("settingsForm")?.addEventListener("submit", connectWifi);
  document.getElementById("fanSpeedSlider")?.addEventListener("input", (event) => {
    const value = event.target.value;
    setText("fanSpeedValue", `${value}%`);
  });
  document.getElementById("fanSpeedSlider")?.addEventListener("change", (event) => {
    setFanSpeed(event.target.value);
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
