"use strict";

const sources = DRIVER_SOURCES;

const categories = {
  "全部": [],
  "整机": ["整机"],
  "主板": ["主板", "芯片组"],
  "显卡": ["显卡", "核显"],
  "网络": ["网络", "网卡", "蓝牙"],
  "音频": ["声卡", "音频"],
  "打印与外设": ["打印与外设", "打印机", "扫描仪", "鼠标", "键盘", "读卡器", "摄像头"]
};
const vendorPatterns = {
  PCI: /^PCI\\VEN_([0-9A-F]{4})&DEV_[0-9A-F]{4}(?=&|$)/i,
  USB: /^USB\\VID_([0-9A-F]{4})&PID_[0-9A-F]{4}(?=&|$)/i
};
const vendors = {
  PCI: {"10DE": "NVIDIA", "1002": "AMD", "1022": "AMD", "8086": "Intel", "10EC": "Realtek", "14C3": "MediaTek"},
  USB: {"0BDA": "Realtek", "04B8": "Epson", "04A9": "Canon", "04F9": "Brother", "046D": "Logitech", "03F0": "HP"}
};
const catalog = "https://catalog.update.microsoft.com/Search.aspx";
const byId = (id) => document.getElementById(id);
const query = byId("query");
let category = "全部";
let term = "";

function element(tag, className, content) {
  const item = document.createElement(tag);
  if (className) item.className = className;
  if (content) item.textContent = content;
  return item;
}

function externalLink(label, url) {
  const link = element("a", "", label + " ↗");
  link.href = url;
  link.target = "_blank";
  link.rel = "noopener noreferrer";
  return link;
}

function identifyHardware(value) {
  const id = value.trim().toUpperCase();
  const bus = id.split("\\", 1)[0];
  const match = Object.hasOwn(vendorPatterns, bus) ? id.match(vendorPatterns[bus]) : null;
  const isDeviceId = /^[A-Z][A-Z0-9_-]*\\[A-Z0-9_&+.{}-]+$/.test(id);
  return isDeviceId ? {id, baseId: match?.[0], brand: match ? vendors[bus][match[1]] : undefined} : null;
}

function cardFor([name, brand, group, description, url, region]) {
  const card = element("article", "card");
  const top = element("div", "card-top");
  top.append(element("span", "tile", brand.slice(0, 3).toUpperCase()), element("span", "region", region));
  const link = externalLink("打开下载页", url);
  link.setAttribute("aria-label", "打开 " + name + " 下载页");
  card.append(top, element("h3", "", name + " · " + brand), element("p", "", description), link);
  return card;
}

function render() {
  const hardware = identifyHardware(term);
  const words = term.toLowerCase().split(/\s+/).filter(Boolean);
  const matching = sources.filter(([name, brand, group, description]) => {
    const haystack = [name, brand, group, description].join(" ").toLowerCase();
    const inCategory = category === "全部" || categories[category].some((word) => haystack.includes(word));
    const knownBrand = words.includes(brand.toLowerCase()) || term.startsWith(name);
    const matchesText = knownBrand || words.every((word) => haystack.includes(word));
    return inCategory && (hardware ? brand === hardware.brand : matchesText);
  });
  const lookup = byId("lookup");
  lookup.replaceChildren();
  lookup.hidden = !term;
  if (term) {
    const url = new URL(catalog);
    url.searchParams.set("q", hardware?.id ?? term);
    const message = hardware
      ? hardware.brand ? "已识别可能的厂商 " + hardware.brand + "。请核对具体型号后下载。" : "可用硬件 ID 在微软更新目录继续查找。"
      : "也可在微软更新目录按关键词或完整型号查找。";
    lookup.append(element("p", "", message), externalLink("搜索微软更新目录", url.href));
    if (hardware?.baseId && hardware.baseId !== hardware.id) {
      url.searchParams.set("q", hardware.baseId);
      lookup.append(externalLink("按基础硬件 ID 扩大搜索", url.href));
    }
  }
  byId("cards").replaceChildren(...matching.map(cardFor));
  byId("status").textContent = matching.length
    ? "找到 " + matching.length + " 个下载入口"
    : "当前目录没有匹配入口，可调整筛选或使用微软更新目录。";
  byId("reset").hidden = !term && category === "全部";
  for (const button of byId("filters").children) button.setAttribute("aria-pressed", String(button.textContent === category));
}

function search(value) {
  const next = value.trim();
  query.value = next;
  term = next;
  category = "全部";
  render();
  byId("sources").scrollIntoView();
}

for (const key of Object.keys(categories)) {
  const button = element("button", "", key);
  button.addEventListener("click", () => {
    category = key;
    render();
  });
  byId("filters").append(button);
}
byId("search").addEventListener("submit", (event) => {
  event.preventDefault();
  search(query.value);
});
for (const button of document.querySelectorAll("[data-query]")) {
  button.addEventListener("click", () => search(button.dataset.query));
}
byId("reset").addEventListener("click", () => search(""));

const report = byId("report");
const importButton = byId("import-button");
importButton.addEventListener("click", () => report.click());
report.addEventListener("change", async () => {
  const file = report.files[0];
  if (!file) return;
  report.value = "";
  importButton.disabled = true;
  const list = byId("device-list");
  list.replaceChildren();
  byId("devices").hidden = false;
  try {
    if (file.size > 4 * 1024 * 1024) throw new Error("设备清单超过 4 MB。");
    let data;
    try {
      data = JSON.parse(await file.text());
    } catch {
      throw new Error("无法读取 JSON，请选择检测脚本生成的设备清单。");
    }
    if (data?.schemaVersion !== 1 || !Array.isArray(data.devices)) {
      throw new Error("设备清单格式不正确。");
    }
    const buttons = [];
    const computer = data.computer;
    if (computer && typeof computer.model === "string" && typeof computer.manufacturer === "string") {
      const aliases = [
        [/lenovo/i, "Lenovo"], [/dell/i, "Dell"], [/hewlett.packard|^hp$/i, "HP"],
        [/asus/i, "ASUS"], [/huawei/i, "Huawei"], [/acer/i, "Acer"], [/micro.star|^msi$/i, "MSI"],
        [/gigabyte/i, "GIGABYTE"]
      ];
      const alias = aliases.find(([pattern]) => pattern.test(computer.manufacturer))?.[1];
      const os = [computer.os, computer.architecture].filter((value) => typeof value === "string").join(" · ");
      const label = [computer.manufacturer, computer.model, os].filter(Boolean).join(" · ");
      if (label && (alias || computer.model)) buttons.push(deviceButton(label, alias || computer.model));
    }
    let count = 0;
    for (const device of data.devices) {
      if (typeof device?.name !== "string" || !Array.isArray(device.ids)) continue;
      const id = device.ids.find((value) => typeof value === "string" && identifyHardware(value));
      if (!id) continue;
      buttons.push(deviceButton(device.name + " · " + id, id));
      count += 1;
    }
    list.append(...buttons.slice(0, 1000));
    byId("device-status").textContent = count
      ? "已读取 " + count + " 个设备。选择一项查找驱动。" + (buttons.length > 1000 ? "最多显示前 1000 项。" : "")
      : "清单中没有可查询的硬件 ID。";
  } catch (error) {
    byId("device-status").textContent = error.message;
  } finally {
    importButton.disabled = false;
  }
});

function deviceButton(label, value) {
  const button = element("button", "device");
  button.append(element("span", "", label), element("span", "", "查找 ↗"));
  button.addEventListener("click", () => search(value));
  return button;
}

render();
