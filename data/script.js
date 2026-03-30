function showSection(sectionId, event) {
  document.querySelectorAll(".section").forEach((section) => {
    section.style.display = "none";
  });

  const target = document.getElementById(sectionId);
  if (target) target.style.display = "block";

  document.querySelectorAll(".nav-item").forEach((item) => {
    item.classList.remove("active");
  });

  if (event) event.currentTarget.classList.add("active");

  if (sectionId === "settings") {
    scanWifi();
    loadWifiStatus();
  }

  if (sectionId === "device") {
    loadDeviceState();
  }
}

function setText(id, value) {
  const el = document.getElementById(id);
  if (el) el.textContent = value;
}

function setStatusBadge(id, isOn) {
  const el = document.getElementById(id);
  if (!el) return;

  el.textContent = isOn ? "ON" : "OFF";
  el.classList.remove("on", "off");
  el.classList.add(isOn ? "on" : "off");
}

function applyDeviceState(elementId, badgeId, state) {
  const isOn = String(state || "OFF").toUpperCase() === "ON";

  const el = document.getElementById(elementId);
  if (el) {
    el.textContent = isOn ? "ON" : "OFF";
    el.classList.remove("on", "off");
    el.classList.add(isOn ? "on" : "off");
  }

  const badge = document.getElementById(badgeId);
  if (badge) {
    badge.textContent = isOn ? "ON" : "OFF";
    badge.classList.remove("on", "off");
    badge.classList.add(isOn ? "on" : "off");
  }
}

function applyDoorState(state) {
  const value = String(state || "--").toUpperCase();
  setText("doorState", value);
}

function applyFanState(data) {
  const isFanOn =
    data.fan_on === true ||
    String(data.fan || "OFF").toUpperCase() === "ON";

  const speed =
    Number.isFinite(Number(data.fan_speed))
      ? Number(data.fan_speed)
      : Number.isFinite(Number(data.speed))
      ? Number(data.speed)
      : 0;

  setStatusBadge("fanState", isFanOn);
  setText("fanSpeedValue", `${speed}%`);

  const slider = document.getElementById("fanSpeedSlider");
  if (slider) slider.value = speed;
}

async function loadSensors() {
  try {
    const response = await fetch(`/sensors?t=${Date.now()}`, {
      method: "GET",
      cache: "no-store"
    });

    if (!response.ok) {
      throw new Error(`HTTP ${response.status}`);
    }

    const data = await response.json();

    setText(
      "temp",
      Number.isFinite(Number(data.temp)) ? `${Number(data.temp).toFixed(1)}°` : "--"
    );

    setText(
      "hum",
      Number.isFinite(Number(data.hum)) ? `${Number(data.hum).toFixed(1)}%` : "--"
    );

    applyDeviceState("led1", "led1Badge", data.led1 || "OFF");
    applyDeviceState("led2", "led2Badge", data.led2 || "OFF");

    setText("deviceIp", window.location.host);
    setText("wifiStatus", "Đang hoạt động");

    applyDoorState(data.door);

    // nếu /sensors có trả fan thì cập nhật luôn
    if (
      data.fan !== undefined ||
      data.fan_on !== undefined ||
      data.fan_speed !== undefined ||
      data.speed !== undefined
    ) {
      applyFanState(data);
    }
  } catch (error) {
    console.error("Không lấy được dữ liệu cảm biến:", error);
    setText("temp", "--");
    setText("hum", "--");
    setText("wifiStatus", "Mất kết nối");
    setText("doorState", "--");
  }
}

async function loadDeviceState() {
  try {
    const res = await fetch(`/state?t=${Date.now()}`, {
      method: "GET",
      cache: "no-store"
    });

    if (!res.ok) {
      throw new Error(`HTTP ${res.status}`);
    }

    const data = await res.json();

    // LED
    if (typeof data.led1 === "boolean") {
      setStatusBadge("led1", data.led1);
      setStatusBadge("led1Badge", data.led1);
    } else {
      applyDeviceState("led1", "led1Badge", data.led1 || "OFF");
    }

    if (typeof data.led2 === "boolean") {
      setStatusBadge("led2", data.led2);
      setStatusBadge("led2Badge", data.led2);
    } else {
      applyDeviceState("led2", "led2Badge", data.led2 || "OFF");
    }

    applyDoorState(data.door);
    applyFanState(data);
  } catch (err) {
    console.error("Không lấy được trạng thái thiết bị:", err);
  }
}

async function setDevice(deviceNumber) {
  try {
    const response = await fetch(`/toggle?led=${deviceNumber}&t=${Date.now()}`, {
      method: "GET",
      cache: "no-store"
    });

    if (!response.ok) {
      throw new Error(`HTTP ${response.status}`);
    }

    const data = await response.json();

    applyDeviceState("led1", "led1Badge", data.led1 || "OFF");
    applyDeviceState("led2", "led2Badge", data.led2 || "OFF");

    // đồng bộ lại toàn bộ state
    await loadDeviceState();
  } catch (error) {
    console.error("Không điều khiển được thiết bị:", error);
    alert("Điều khiển thất bại");
  }
}

async function controlDoor(state) {
  try {
    const res = await fetch(`/door?state=${state}&t=${Date.now()}`, {
      method: "GET",
      cache: "no-store"
    });

    if (!res.ok) {
      throw new Error(`HTTP ${res.status}`);
    }

    const data = await res.json();
    applyDoorState(data.door);
  } catch (err) {
    console.error("Door control error:", err);
    alert("Lỗi điều khiển cửa");
  }
}

async function setFan(state) {
  try {
    const res = await fetch(`/fan?state=${state}&t=${Date.now()}`, {
      method: "GET",
      cache: "no-store"
    });

    if (!res.ok) {
      throw new Error(`HTTP ${res.status}`);
    }

    await loadDeviceState();
  } catch (err) {
    console.error("Fan control error:", err);
    alert("Không điều khiển được quạt");
  }
}

async function setFanSpeed(value) {
  try {
    const res = await fetch(`/fan?speed=${value}&t=${Date.now()}`, {
      method: "GET",
      cache: "no-store"
    });

    if (!res.ok) {
      throw new Error(`HTTP ${res.status}`);
    }

    await loadDeviceState();
  } catch (err) {
    console.error("Fan speed error:", err);
    alert("Không chỉnh được tốc độ quạt");
  }
}

async function connectWifi(event) {
  if (event) event.preventDefault();

  const ssid = document.getElementById("ssid")?.value?.trim() || "";
  const password = document.getElementById("password")?.value || "";
  const btn = document.querySelector('#settingsForm button[type="submit"]');

  if (!ssid) {
    alert("Vui lòng nhập SSID");
    return;
  }

  try {
    if (btn) {
      btn.disabled = true;
      btn.textContent = "Đang kết nối...";
    }

    const response = await fetch(
      `/connect?ssid=${encodeURIComponent(ssid)}&pass=${encodeURIComponent(password)}&t=${Date.now()}`,
      {
        method: "GET",
        cache: "no-store"
      }
    );

    if (!response.ok) {
      throw new Error(`HTTP ${response.status}`);
    }

    const data = await response.json().catch(() => null);

    if (!data || data.ok !== true) {
      throw new Error(data?.error || "Phản hồi không hợp lệ");
    }

    setText("wifiStatus", "Đang kết nối Wi-Fi...");
    alert("Thiết bị đã nhận cấu hình Wi-Fi, đang thử kết nối");

    waitForWifiConnection();
  } catch (error) {
    console.error("Kết nối Wi-Fi lỗi:", error);
    setText("wifiStatus", "Gửi cấu hình thất bại");
    alert("Không gửi được cấu hình Wi-Fi");
  } finally {
    if (btn) {
      btn.disabled = false;
      btn.textContent = "Kết nối Wi-Fi";
    }
  }
}

async function waitForWifiConnection() {
  const maxAttempts = 20;

  for (let i = 0; i < maxAttempts; i++) {
    try {
      const res = await fetch(`/wifi_status?t=${Date.now()}`, {
        method: "GET",
        cache: "no-store"
      });

      if (!res.ok) {
        throw new Error(`HTTP ${res.status}`);
      }

      const data = await res.json();

      if (data.connecting) {
        setText("wifiStatus", "Đang kết nối Wi-Fi...");
      }

      if (data.connected) {
        setText("wifiStatus", `Đã kết nối: ${data.ssid || ""}`);

        if (data.sta_ip) {
          alert(`Kết nối Wi-Fi thành công\nIP mới của thiết bị: ${data.sta_ip}`);
        } else {
          alert("Kết nối Wi-Fi thành công");
        }
        return;
      }

      if (!data.connecting && !data.connected && data.message) {
        setText("wifiStatus", data.message);
      }
    } catch (err) {
      console.error("wifi_status error:", err);
    }

    await new Promise((resolve) => setTimeout(resolve, 1500));
  }

  setText("wifiStatus", "Kết nối Wi-Fi thất bại hoặc timeout");
  alert("Thiết bị chưa kết nối được Wi-Fi. Hãy kiểm tra SSID và mật khẩu.");
}

async function loadWifiStatus() {
  try {
    const res = await fetch(`/wifi_status?t=${Date.now()}`, {
      method: "GET",
      cache: "no-store"
    });

    if (!res.ok) {
      throw new Error(`HTTP ${res.status}`);
    }

    const data = await res.json();

    if (data.connected) {
      setText("wifiStatus", `Đã kết nối: ${data.ssid || ""}`);
    } else if (data.connecting) {
      setText("wifiStatus", "Đang kết nối Wi-Fi...");
    } else {
      setText("wifiStatus", data.message || "Chưa kết nối");
    }
  } catch (e) {
    console.error("Không lấy được trạng thái Wi-Fi:", e);
  }
}

async function scanWifi() {
  try {
    const res = await fetch(`/scan_wifi?t=${Date.now()}`, {
      method: "GET",
      cache: "no-store"
    });

    if (!res.ok) {
      throw new Error(`HTTP ${res.status}`);
    }

    const data = await res.json();
    const list = document.getElementById("wifiList");
    if (!list) return;

    list.innerHTML = "";

    if (!Array.isArray(data) || data.length === 0) {
      list.innerHTML = `<div class="wifi-item"><strong>Không tìm thấy mạng Wi-Fi</strong><span>Thử quét lại</span></div>`;
      return;
    }

    data.sort((a, b) => b.rssi - a.rssi);

    data.forEach((net) => {
      const item = document.createElement("div");
      item.className = "wifi-item";
      item.innerHTML = `
        <strong>${net.ssid}</strong>
        <span>${net.rssi} dBm ${net.secure ? "🔒" : "🔓"}</span>
      `;

      item.onclick = () => {
        const ssidInput = document.getElementById("ssid");
        if (ssidInput) ssidInput.value = net.ssid;
      };

      list.appendChild(item);
    });
  } catch (e) {
    console.error("Scan wifi lỗi:", e);
    const list = document.getElementById("wifiList");
    if (list) {
      list.innerHTML = `<div class="wifi-item"><strong>Không quét được Wi-Fi</strong><span>Kiểm tra lại kết nối</span></div>`;
    }
  }
}

window.addEventListener("DOMContentLoaded", () => {
  const settingsForm = document.getElementById("settingsForm");
  if (settingsForm) {
    settingsForm.addEventListener("submit", connectWifi);
  }

  loadSensors();
  loadDeviceState();
  loadWifiStatus();
  scanWifi();

  setInterval(loadSensors, 2000);
  setInterval(loadDeviceState, 3000);
  setInterval(loadWifiStatus, 3000);
});

function savePcIp() {
  const ip = document.getElementById('pcIpInput').value.trim();
  if (!ip) {
    alert("Vui lòng nhập IP PC");
    return;
  }

  localStorage.setItem("pc_ip", ip);
  document.getElementById("camStatus").innerText = "Đã lưu IP: " + ip;
}

function connectCamera() {
  let ip = document.getElementById('pcIpInput').value.trim();

  if (!ip) {
    ip = localStorage.getItem("pc_ip");
  }

  if (!ip) {
    alert("Chưa có IP PC");
    return;
  }

  const url = `http://${ip}/video_feed`;

  const img = document.getElementById("cameraView");
  img.src = url;

  document.getElementById("camStatus").innerText =
    "Đang kết nối: " + url;
}

function disconnectCamera() {
  document.getElementById("cameraView").src = "";
  document.getElementById("camStatus").innerText = "Đã ngắt camera";
}

window.addEventListener("load", () => {
  const saved = localStorage.getItem("pc_ip");
  if (saved) {
    document.getElementById("pcIpInput").value = saved;
  }
});