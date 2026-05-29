#include "PortalManager.h"

#include <ArduinoJson.h>

namespace {
const char *kAccessPointName = "CyberMonitor-Setup";
const uint32_t kDefaultFetchIntervalMs = 2000;
const uint32_t kProvisioningTimeoutMs = 20000;
}

PortalManager::PortalManager(
    ConfigManager &configManagerRef, DisplayManager &displayManagerRef)
    : configManager(configManagerRef),
      displayManager(displayManagerRef),
      server(80) {}

void PortalManager::begin() {
  configureRoutes();

  if (!serverStarted) {
    server.begin();
    serverStarted = true;
  }
}

bool PortalManager::connectToWifi(
    const AppConfig &config, uint32_t timeoutMs, String &localIp) {
  begin();

  // 正常运行模式不需要保留热点，直接切换到纯 STA。
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(false);
  WiFi.begin(config.wifiSsid.c_str(), config.wifiPassword.c_str());

  const uint32_t startedAt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startedAt < timeoutMs) {
    delay(250);
    server.handleClient();
  }

  if (WiFi.status() != WL_CONNECTED) {
    return false;
  }

  localIp = WiFi.localIP().toString();
  return true;
}

void PortalManager::startAccessPoint(const AppConfig &currentConfig) {
  begin();

  // 进入热点模式时重置配网向导上下文，确保上一轮失败状态不会遗留。
  lastShownConfig = currentConfig;
  portalWifiSsid = currentConfig.wifiSsid;
  portalWifiPassword = currentConfig.wifiPassword;
  portalLocalIp = "";
  portalSubnetMask = "";
  portalGateway = "";
  portalSubnetPrefix = "";
  portalErrorMessage = "";
  provisioningState = ProvisioningState::Idle;
  provisioningKickoffPending = false;
  provisioningStartedAtMs = 0;

  WiFi.disconnect(true, true);
  WiFi.mode(WIFI_AP);
  WiFi.softAP(kAccessPointName);
}

void PortalManager::handleClient() {
  server.handleClient();
  handleProvisioningLoop();
}

bool PortalManager::consumeRestartRequested() {
  const bool shouldRestart = restartRequested;
  restartRequested = false;
  return shouldRestart;
}

String PortalManager::getApIp() const {
  return WiFi.softAPIP().toString();
}

void PortalManager::configureRoutes() {
  if (routesConfigured) {
    return;
  }

  server.on("/", HTTP_GET, [this]() {
    server.send(200, "text/html; charset=utf-8", buildConfigPage(lastShownConfig));
  });

  server.on("/api/scan", HTTP_GET, [this]() {
    server.send(200, "application/json", buildScanJson());
  });

  server.on("/api/wifi-status", HTTP_GET, [this]() {
    server.send(200, "application/json", buildStatusJson());
  });

  server.on("/api/connect-wifi", HTTP_POST, [this]() {
    const String ssid = server.arg("wifi_ssid");
    const String password = server.arg("wifi_password");

    if (ssid.length() == 0) {
      server.send(
          400, "application/json", "{\"ok\":false,\"message\":\"wifi_ssid is required\"}");
      return;
    }

    startWifiProvisioning(ssid, password);
    server.send(200, "application/json", "{\"ok\":true}");
  });

  server.on("/api/save-prometheus", HTTP_POST, [this]() {
    if (provisioningState != ProvisioningState::Connected) {
      server.send(
          400,
          "text/html; charset=utf-8",
          buildResultPage("WiFi Not Ready", "Finish WiFi connection before saving Prometheus."));
      return;
    }

    AppConfig newConfig;
    newConfig.wifiSsid = portalWifiSsid;
    newConfig.wifiPassword = portalWifiPassword;
    newConfig.prometheusHost = server.arg("prometheus_host");
    newConfig.prometheusInstance = server.arg("prometheus_instance");
    newConfig.gpuPhysicalIndex = server.arg("gpu_phys");

    const int port = server.arg("prometheus_port").toInt();
    const int interval = server.arg("fetch_interval_ms").toInt();
    const int instancePort = server.arg("instance_port").toInt();

    if (newConfig.gpuPhysicalIndex.length() == 0) {
      newConfig.gpuPhysicalIndex = "0";
    }

    // 前端会优先拼出完整 instance，但为了兼容旧浏览器或手工请求，
    // 这里保留一个后备逻辑：如果 instance 为空，则尝试根据 host 和端口拼装。
    if (newConfig.prometheusInstance.length() == 0 &&
        newConfig.prometheusHost.length() > 0 && instancePort > 0) {
      newConfig.prometheusInstance =
          newConfig.prometheusHost + ":" + String(instancePort);
    }

    newConfig.prometheusPort = port > 0 ? static_cast<uint16_t>(port) : 9090;
    newConfig.fetchIntervalMs =
        interval >= 500 ? static_cast<uint32_t>(interval) : kDefaultFetchIntervalMs;

    if (!newConfig.isValid()) {
      server.send(
          400,
          "text/html; charset=utf-8",
          buildResultPage(
              "Invalid Config",
              "WiFi, Prometheus host and Prometheus instance must all be provided."));
      return;
    }

    const bool saved = configManager.save(newConfig);
    if (!saved) {
      server.send(
          500,
          "text/html; charset=utf-8",
          buildResultPage("Save Failed", "Preferences write failed. Please retry."));
      return;
    }

    lastShownConfig = newConfig;
    restartRequested = true;
    displayManager.showRestarting();

    server.send(
        200,
        "text/html; charset=utf-8",
        buildResultPage("Saved", "Configuration saved. Device will restart."));
  });

  routesConfigured = true;
}

void PortalManager::handleProvisioningLoop() {
  if (provisioningState != ProvisioningState::Connecting) {
    return;
  }

  if (provisioningKickoffPending) {
    // 配网向导阶段使用 AP+STA 双模式：
    // 1. 保持热点不掉，让用户手机始终还能访问 192.168.4.1
    // 2. 同时尝试连接家庭 Wi-Fi，以便自动推导局域网前缀
    WiFi.mode(WIFI_AP_STA);
    WiFi.setAutoReconnect(false);
    WiFi.persistent(false);
    WiFi.begin(portalWifiSsid.c_str(), portalWifiPassword.c_str());

    provisioningKickoffPending = false;
    provisioningStartedAtMs = millis();
    displayManager.showSetupWifiProgress("Trying " + portalWifiSsid);
  }

  if (WiFi.status() == WL_CONNECTED) {
    portalLocalIp = WiFi.localIP().toString();
    portalSubnetMask = WiFi.subnetMask().toString();
    portalGateway = WiFi.gatewayIP().toString();
    portalSubnetPrefix = buildSubnetPrefix(WiFi.localIP(), WiFi.subnetMask());
    portalErrorMessage = "";
    provisioningState = ProvisioningState::Connected;

    displayManager.showSetupWifiResult(portalWifiSsid, portalLocalIp, portalSubnetMask);
    return;
  }

  if (millis() - provisioningStartedAtMs > kProvisioningTimeoutMs) {
    portalErrorMessage = "WiFi connect timeout";
    provisioningState = ProvisioningState::Failed;
    displayManager.showSetupWifiProgress("Connect failed");

    // 失败后保留 AP，断开 STA，等待用户重新尝试。
    WiFi.disconnect();
    WiFi.mode(WIFI_AP);
    WiFi.softAP(kAccessPointName);
  }
}

void PortalManager::startWifiProvisioning(const String &ssid, const String &password) {
  portalWifiSsid = ssid;
  portalWifiPassword = password;
  portalLocalIp = "";
  portalSubnetMask = "";
  portalGateway = "";
  portalSubnetPrefix = "";
  portalErrorMessage = "";
  provisioningState = ProvisioningState::Connecting;
  provisioningKickoffPending = true;
}

String PortalManager::buildConfigPage(const AppConfig &currentConfig) const {
  String html = R"HTML(
<!doctype html>
<html lang="zh-CN">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>CyberMonitor Setup</title>
  <style>
    :root{
      --bg:#0c1116;
      --panel:#141d26;
      --line:#2d3a46;
      --text:#eef4fa;
      --muted:#98a9b8;
      --accent:#53e4a7;
      --accent-2:#67d5ff;
      --warn:#ffb347;
    }
    *{box-sizing:border-box}
    body{margin:0;padding:20px;background:radial-gradient(circle at top,#1a2530 0,#0c1116 48%);color:var(--text);font-family:monospace}
    .shell{max-width:760px;margin:0 auto}
    .panel{background:rgba(20,29,38,.96);border:1px solid var(--line);padding:20px;margin-bottom:18px;box-shadow:0 14px 40px rgba(0,0,0,.24)}
    h1,h2{margin:0 0 12px}
    p{margin:8px 0;color:var(--muted);line-height:1.6}
    .grid{display:grid;gap:12px}
    .grid.two{grid-template-columns:repeat(auto-fit,minmax(220px,1fr))}
    label{display:block;margin:10px 0 6px;color:#d6e3ef}
    input,select,button{width:100%;padding:12px;border:1px solid var(--line);background:#0a1015;color:var(--text);font:inherit}
    button{background:linear-gradient(90deg,var(--accent),#9bfaa8);color:#07110b;font-weight:700;cursor:pointer}
    button.secondary{background:#111922;color:var(--text)}
    .badge{display:inline-block;padding:4px 8px;border:1px solid var(--line);color:var(--accent-2);margin-bottom:10px}
    .row{display:flex;gap:10px;align-items:center}
    .prefix{padding:12px;border:1px solid var(--line);background:#111922;color:var(--accent-2);min-width:110px;text-align:center}
    .hint{font-size:13px;color:var(--muted)}
    .status{padding:12px;border:1px solid var(--line);background:#101720;margin-top:12px;white-space:pre-wrap}
    .ok{color:var(--accent)}
    .warn{color:var(--warn)}
    .hidden{display:none}
    ul{margin:8px 0 0 18px;color:var(--muted)}
  </style>
</head>
<body>
  <div class="shell">
    <div class="panel">
      <div class="badge">CyberMonitor Onboarding</div>
      <h1>ESP32 物理监控终端配网向导</h1>
      <p>第一步先让 ESP32 连上你的 Wi-Fi。连上后，它会读取自己的局域网 IP 和子网掩码，自动帮你补出 Prometheus 与 exporter 地址的网段前缀。</p>
    </div>

    <div class="panel">
      <h2>Step 1. 连接 Wi-Fi</h2>
      <p>可以直接选择扫描到的 SSID，也可以手动输入隐藏网络。</p>
      <form id="wifi-form">
        <label>扫描到的 Wi-Fi</label>
        <div class="grid two">
          <select id="ssid-select">
            <option value="">手动输入或点右侧刷新</option>
          </select>
          <button type="button" class="secondary" id="scan-btn">刷新 SSID 列表</button>
        </div>

        <label>Wi-Fi SSID</label>
        <input id="wifi-ssid" value="%WIFI_SSID%" placeholder="例如 MyHomeWiFi" required>

        <label>Wi-Fi 密码</label>
        <input id="wifi-password" type="password" placeholder="输入 Wi-Fi 密码">

        <button type="submit">让 ESP32 连接这个 Wi-Fi</button>
      </form>

      <div id="wifi-status" class="status">等待开始配网...</div>
    </div>

    <div class="panel hidden" id="prom-panel">
      <h2>Step 2. 配置 Prometheus 与 exporter</h2>
      <p>下面的网段前缀会根据 ESP32 实际连接到的局域网自动推导。你只需要补最后一段，或者直接改成完整地址。</p>

      <form method="post" action="/api/save-prometheus" id="prom-form">
        <label>Prometheus Host</label>
        <div class="row">
          <div class="prefix" id="prom-prefix">自动推导中</div>
          <input id="prom-last-octet" placeholder="最后一段，例如 10">
        </div>
        <p class="hint">如果 Prometheus 不在同一网段，可以直接修改下面的完整地址。</p>
        <input id="prometheus-host" name="prometheus_host" value="%PROM_HOST%" placeholder="例如 192.168.1.10" required>

        <label>Prometheus Port</label>
        <input type="number" name="prometheus_port" value="%PROM_PORT%" placeholder="9090">

        <label>Exporter Instance Host</label>
        <div class="row">
          <div class="prefix" id="instance-prefix">自动推导中</div>
          <input id="instance-last-octet" placeholder="最后一段，例如 50">
        </div>
        <p class="hint">这里是 Prometheus 中目标主机的 instance 地址。通常就是 windows_exporter 所在主机。</p>
        <input id="prometheus-instance" name="prometheus_instance" value="%PROM_INSTANCE%" placeholder="例如 192.168.1.50:9182" required>

        <label>Exporter Instance Port</label>
        <input id="instance-port" type="number" name="instance_port" value="9182" placeholder="9182">

        <div class="grid two">
          <div>
            <label>GPU Physical Index</label>
            <input name="gpu_phys" value="%GPU_PHYS%" placeholder="0">
          </div>
          <div>
            <label>抓取间隔（毫秒）</label>
            <input type="number" name="fetch_interval_ms" value="%FETCH_INT%" min="500">
          </div>
        </div>

        <button type="submit">保存并重启 ESP32</button>
      </form>
    </div>

    <div class="panel">
      <h2>当前说明</h2>
      <ul>
        <li>默认面向 Windows PC，Prometheus 查询口径基于 windows_exporter。</li>
        <li>CPU 温度需要 thermalzone collector 有数据。</li>
        <li>GPU 利用率需要 gpu collector 有数据。</li>
      </ul>
    </div>
  </div>

  <script>
    const wifiForm = document.getElementById('wifi-form');
    const ssidSelect = document.getElementById('ssid-select');
    const wifiSsidInput = document.getElementById('wifi-ssid');
    const wifiPasswordInput = document.getElementById('wifi-password');
    const wifiStatusBox = document.getElementById('wifi-status');
    const promPanel = document.getElementById('prom-panel');
    const scanBtn = document.getElementById('scan-btn');
    const promPrefix = document.getElementById('prom-prefix');
    const instancePrefix = document.getElementById('instance-prefix');
    const promLastOctet = document.getElementById('prom-last-octet');
    const instanceLastOctet = document.getElementById('instance-last-octet');
    const promHostInput = document.getElementById('prometheus-host');
    const promInstanceInput = document.getElementById('prometheus-instance');
    const instancePortInput = document.getElementById('instance-port');

    let inferredPrefix = '';

    function updateStatus(text, className) {
      wifiStatusBox.textContent = text;
      wifiStatusBox.className = 'status' + (className ? ' ' + className : '');
    }

    function applyPrefix(prefix) {
      inferredPrefix = prefix || '';
      promPrefix.textContent = inferredPrefix || '请手动填写';
      instancePrefix.textContent = inferredPrefix || '请手动填写';
    }

    function fillFromOctets() {
      if (inferredPrefix && promLastOctet.value.trim()) {
        promHostInput.value = inferredPrefix + promLastOctet.value.trim();
      }
      if (inferredPrefix && instanceLastOctet.value.trim()) {
        promInstanceInput.value =
          inferredPrefix + instanceLastOctet.value.trim() + ':' + instancePortInput.value.trim();
      }
    }

    async function loadScan() {
      updateStatus('正在扫描附近 Wi-Fi ...');
      const response = await fetch('/api/scan');
      const payload = await response.json();

      ssidSelect.innerHTML = '<option value="">手动输入或选择下方 SSID</option>';
      (payload.networks || []).forEach((network) => {
        const option = document.createElement('option');
        option.value = network.ssid;
        option.textContent = network.ssid + '  (' + network.rssi + ' dBm)';
        ssidSelect.appendChild(option);
      });

      updateStatus('SSID 列表已刷新，可直接选择或手动输入。');
    }

    async function pollStatus() {
      const response = await fetch('/api/wifi-status');
      const payload = await response.json();

      if (payload.state === 'connecting') {
        updateStatus('ESP32 正在连接 Wi-Fi：' + (payload.ssid || ''), 'warn');
        return;
      }

      if (payload.state === 'failed') {
        updateStatus('连接失败：' + (payload.message || '未知错误'), 'warn');
        promPanel.classList.add('hidden');
        return;
      }

      if (payload.state === 'connected') {
        updateStatus(
          'Wi-Fi 已连接\\nSSID: ' + payload.ssid +
          '\\nLocal IP: ' + payload.local_ip +
          '\\nSubnet Mask: ' + payload.subnet_mask +
          '\\nGateway: ' + payload.gateway,
          'ok'
        );

        applyPrefix(payload.subnet_prefix || '');
        if (payload.subnet_prefix && !promLastOctet.value && !promHostInput.value) {
          promHostInput.value = payload.subnet_prefix;
        }
        if (payload.subnet_prefix && !promInstanceInput.value) {
          promInstanceInput.value = payload.subnet_prefix;
        }
        promPanel.classList.remove('hidden');
      }
    }

    ssidSelect.addEventListener('change', () => {
      if (ssidSelect.value) {
        wifiSsidInput.value = ssidSelect.value;
      }
    });

    scanBtn.addEventListener('click', () => {
      loadScan().catch((error) => updateStatus('扫描失败：' + error.message, 'warn'));
    });

    wifiForm.addEventListener('submit', async (event) => {
      event.preventDefault();
      updateStatus('已提交 Wi-Fi 凭据，ESP32 正在尝试连接 ...', 'warn');
      promPanel.classList.add('hidden');

      const body = new URLSearchParams();
      body.set('wifi_ssid', wifiSsidInput.value.trim());
      body.set('wifi_password', wifiPasswordInput.value);

      const response = await fetch('/api/connect-wifi', {
        method: 'POST',
        headers: {'Content-Type': 'application/x-www-form-urlencoded'},
        body: body.toString()
      });

      if (!response.ok) {
        updateStatus('提交 Wi-Fi 凭据失败。', 'warn');
      }
    });

    promLastOctet.addEventListener('input', fillFromOctets);
    instanceLastOctet.addEventListener('input', fillFromOctets);
    instancePortInput.addEventListener('input', fillFromOctets);

    loadScan().catch((error) => updateStatus('扫描失败：' + error.message, 'warn'));
    setInterval(() => {
      pollStatus().catch(() => {});
    }, 1200);
    pollStatus().catch(() => {});
  </script>
</body>
</html>
)HTML";

  html.replace("%WIFI_SSID%", htmlEscape(currentConfig.wifiSsid));
  html.replace("%PROM_HOST%", htmlEscape(currentConfig.prometheusHost));
  html.replace("%PROM_PORT%", String(currentConfig.prometheusPort));
  html.replace("%PROM_INSTANCE%", htmlEscape(currentConfig.prometheusInstance));
  html.replace("%GPU_PHYS%", htmlEscape(currentConfig.gpuPhysicalIndex));
  html.replace("%FETCH_INT%", String(currentConfig.fetchIntervalMs));
  return html;
}

String PortalManager::buildResultPage(const String &title, const String &message) const {
  String html;
  html.reserve(900);

  html += "<!doctype html><html><head><meta charset='utf-8'>";
  html += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
  html += "<title>" + title + "</title>";
  html += "<style>body{font-family:monospace;background:#0f1419;color:#eef2f6;padding:32px;}";
  html += ".panel{max-width:520px;margin:0 auto;background:#182028;padding:24px;border:1px solid #2a3743;}";
  html += "a{color:#67e8f9;}</style></head><body><div class='panel'>";
  html += "<h1>" + title + "</h1><p>" + message + "</p>";
  html += "<p><a href='/'>Back to config</a></p></div></body></html>";

  return html;
}

String PortalManager::buildStatusJson() const {
  String state = "idle";
  switch (provisioningState) {
    case ProvisioningState::Idle:
      state = "idle";
      break;
    case ProvisioningState::Connecting:
      state = "connecting";
      break;
    case ProvisioningState::Connected:
      state = "connected";
      break;
    case ProvisioningState::Failed:
      state = "failed";
      break;
  }

  String json = "{";
  json += "\"state\":\"" + jsonEscape(state) + "\",";
  json += "\"ssid\":\"" + jsonEscape(portalWifiSsid) + "\",";
  json += "\"local_ip\":\"" + jsonEscape(portalLocalIp) + "\",";
  json += "\"subnet_mask\":\"" + jsonEscape(portalSubnetMask) + "\",";
  json += "\"gateway\":\"" + jsonEscape(portalGateway) + "\",";
  json += "\"subnet_prefix\":\"" + jsonEscape(portalSubnetPrefix) + "\",";
  json += "\"message\":\"" + jsonEscape(portalErrorMessage) + "\"";
  json += "}";
  return json;
}

String PortalManager::buildScanJson() {
  // 扫描结果只用于改善配网体验，因此这里采用“即时扫描、即时返回”的简单策略。
  const int networkCount = WiFi.scanNetworks();

  String json = "{\"networks\":[";
  for (int i = 0; i < networkCount; ++i) {
    if (i > 0) {
      json += ",";
    }

    json += "{";
    json += "\"ssid\":\"" + jsonEscape(WiFi.SSID(i)) + "\",";
    json += "\"rssi\":" + String(WiFi.RSSI(i));
    json += "}";
  }
  json += "]}";

  WiFi.scanDelete();
  return json;
}

String PortalManager::buildSubnetPrefix(IPAddress localIp, IPAddress subnetMask) const {
  // 这里专门满足“自动帮用户补前几位”的需求。
  // 只有在掩码按字节对齐时，才安全地推导出类似 192.168.1. 这样的前缀。
  String prefix;

  for (int i = 0; i < 4; ++i) {
    if (subnetMask[i] == 255) {
      prefix += String(localIp[i]);
      prefix += ".";
      continue;
    }

    if (subnetMask[i] == 0) {
      break;
    }

    // 遇到例如 /27 这类非整字节掩码时，不强行猜前缀，交给用户手工填写。
    return "";
  }

  return prefix;
}

String PortalManager::htmlEscape(const String &value) {
  String escaped = value;
  escaped.replace("&", "&amp;");
  escaped.replace("<", "&lt;");
  escaped.replace(">", "&gt;");
  escaped.replace("\"", "&quot;");
  escaped.replace("'", "&#39;");
  return escaped;
}

String PortalManager::jsonEscape(const String &value) {
  String escaped = value;
  escaped.replace("\\", "\\\\");
  escaped.replace("\"", "\\\"");
  escaped.replace("\n", "\\n");
  escaped.replace("\r", "");
  return escaped;
}
