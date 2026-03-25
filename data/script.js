function showSection(sectionId, event) {
  document.querySelectorAll(".section").forEach(section => {
    section.style.display = "none";
  });

  const target = document.getElementById(sectionId);
  if (target) target.style.display = "block";

  document.querySelectorAll(".nav-item").forEach(item => {
    item.classList.remove("active");
  });

  if (event) event.currentTarget.classList.add("active");
}

async function loadSensors() {
  try {
    const response = await fetch("/sensors?t=" + Date.now(), {
      method: "GET",
      cache: "no-store"
    });

    if (!response.ok) {
      throw new Error("HTTP " + response.status);
    }

    const data = await response.json();
    console.log("Sensor data:", data);

    const tempEl = document.getElementById("temp");
    const humEl = document.getElementById("hum");
    const led1El = document.getElementById("led1");
    const led2El = document.getElementById("led2");
    const deviceIpEl = document.getElementById("deviceIp");
    const wifiStatusEl = document.getElementById("wifiStatus");

    if (tempEl) tempEl.textContent = Number(data.temp).toFixed(1) + " °C";
    if (humEl) humEl.textContent = Number(data.hum).toFixed(1) + " %";
    if (led1El) led1El.textContent = data.led1 || "--";
    if (led2El) led2El.textContent = data.led2 || "--";
    if (deviceIpEl) deviceIpEl.textContent = window.location.host;
    if (wifiStatusEl) wifiStatusEl.textContent = "Đang hoạt động";
  } catch (error) {
    console.error("Không lấy được dữ liệu cảm biến:", error);

    const tempEl = document.getElementById("temp");
    const humEl = document.getElementById("hum");
    const wifiStatusEl = document.getElementById("wifiStatus");

    if (tempEl) tempEl.textContent = "Lỗi";
    if (humEl) humEl.textContent = "Lỗi";
    if (wifiStatusEl) wifiStatusEl.textContent = "Mất kết nối";
  }
}

async function toggleRelay(relayNumber) {
  try {
    const response = await fetch(`/toggle?led=${relayNumber}&t=${Date.now()}`, {
      method: "GET",
      cache: "no-store"
    });

    if (!response.ok) {
      throw new Error("HTTP " + response.status);
    }

    const data = await response.json();
    console.log("Toggle response:", data);

    const led1El = document.getElementById("led1");
    const led2El = document.getElementById("led2");

    if (led1El) led1El.textContent = data.led1 || "--";
    if (led2El) led2El.textContent = data.led2 || "--";
  } catch (error) {
    console.error("Toggle relay lỗi:", error);
    alert("Không điều khiển được relay");
  }
}

async function connectWifi(event) {
  event.preventDefault();

  const ssid = document.getElementById("ssid")?.value?.trim() || "";
  const password = document.getElementById("password")?.value || "";

  if (!ssid) {
    alert("Vui lòng nhập SSID");
    return;
  }

  try {
    const response = await fetch(
      `/connect?ssid=${encodeURIComponent(ssid)}&pass=${encodeURIComponent(password)}&t=${Date.now()}`,
      { method: "GET", cache: "no-store" }
    );

    const text = await response.text();
    console.log("Connect response:", text);
    alert("Thiết bị đang thử kết nối Wi-Fi");
  } catch (error) {
    console.error("Kết nối Wi-Fi lỗi:", error);
    alert("Không gửi được cấu hình Wi-Fi");
  }
}

window.addEventListener("DOMContentLoaded", () => {
  const settingsForm = document.getElementById("settingsForm");
  if (settingsForm) {
    settingsForm.addEventListener("submit", connectWifi);
  }

  loadSensors();
  setInterval(loadSensors, 2000);
});