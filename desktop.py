"""Driver Source 0.1.0 desktop application, Python 3.10+."""
import json
from pathlib import Path
import platform
import queue
import re
import subprocess
import threading
import tkinter as tk
from tkinter import filedialog, messagebox, ttk
from urllib.parse import urlencode, unquote, urlsplit
import webbrowser

from downloader import Cancelled, download, validate_url

VERSION = "0.1.0"
ROOT = Path(__file__).resolve().parent
SCAN_COMMAND = r"""
$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = New-Object System.Text.UTF8Encoding($false)
$c = Get-CimInstance Win32_ComputerSystem
$o = Get-CimInstance Win32_OperatingSystem
$d = @(Get-CimInstance Win32_PnPEntity -Filter 'Present = TRUE' |
    Where-Object { $_.Name -and $_.HardwareID } | Sort-Object Name |
    ForEach-Object { @{ name = $_.Name; ids = @($_.HardwareID) } })
@{schemaVersion=1; computer=@{manufacturer=$c.Manufacturer; model=$c.Model;
os=$o.Caption; architecture=$o.OSArchitecture}; devices=$d} | ConvertTo-Json -Depth 5
"""


def load_sources():
    text = (ROOT / "sources.js").read_text(encoding="utf-8")
    return json.loads(text.removeprefix("const DRIVER_SOURCES = ").strip().removesuffix(";"))


def validate_report(data):
    if not isinstance(data, dict) or data.get("schemaVersion") != 1 or not isinstance(data.get("devices"), list):
        raise ValueError("请选择检测脚本生成的设备清单。")
    devices = []
    for device in data["devices"]:
        if not isinstance(device, dict) or not isinstance(device.get("name"), str) or not isinstance(device.get("ids"), list):
            continue
        ids = [item for item in device["ids"] if isinstance(item, str) and "\\" in item]
        if ids:
            devices.append((device["name"], ids))
    return devices


def suggested_name(url):
    name = unquote(urlsplit(url).path.rsplit("/", 1)[-1])
    name = re.sub(r'[<>:"/\\|?*\x00-\x1f]', "_", name).strip(". ")
    if not name or name.split(".")[0].upper() in {"CON", "PRN", "AUX", "NUL", *("COM" + str(i) for i in range(1, 10)), *("LPT" + str(i) for i in range(1, 10))}:
        name = "driver-package.bin"
    return name[:180]


class Application:
    def __init__(self, root):
        self.root = root
        self.sources = load_sources()
        self.events = queue.Queue()
        self.cancel = threading.Event()
        self.worker = None
        self.scanning = False
        self.closing = False
        self.devices = []
        root.title("驱动源 " + VERSION)
        root.geometry("1060x760")
        root.minsize(780, 600)
        style = ttk.Style()
        if "clam" in style.theme_names():
            style.theme_use("clam")
        style.configure(".", font=("Microsoft YaHei UI", 10))
        style.configure("Title.TLabel", font=("Microsoft YaHei UI", 22, "bold"))
        outer = ttk.Frame(root, padding=20)
        outer.pack(fill="both", expand=True)
        ttk.Label(outer, text="驱动源", style="Title.TLabel").pack(anchor="w")
        ttk.Label(outer, text="免费查询 · 国内厂商入口优先 · Windows 驱动下载").pack(anchor="w", pady=(0, 12))
        self.tabs = ttk.Notebook(outer)
        self.tabs.pack(fill="both", expand=True)
        self.make_sources()
        self.make_hardware()
        self.make_downloads()
        ttk.Label(outer, text="下载前核对设备型号、Windows 版本与系统架构。品牌整机优先使用整机厂商驱动。",
                  wraplength=960).pack(anchor="w", pady=(10, 0))
        root.protocol("WM_DELETE_WINDOW", self.close)
        root.after(100, self.poll)

    def make_sources(self):
        page = ttk.Frame(self.tabs, padding=12)
        self.tabs.add(page, text="国内驱动源")
        row = ttk.Frame(page)
        row.pack(fill="x")
        self.search = tk.StringVar()
        entry = ttk.Entry(row, textvariable=self.search)
        entry.pack(side="left", fill="x", expand=True)
        entry.bind("<Return>", lambda event: self.filter_sources())
        ttk.Button(row, text="搜索", command=self.filter_sources).pack(side="left", padx=8)
        ttk.Button(row, text="全部", command=self.reset_sources).pack(side="left")
        self.source_tree = ttk.Treeview(page, columns=("brand", "category", "scope", "region"), show="headings")
        for name, title, width in [("brand", "厂商", 170), ("category", "类别", 90), ("scope", "支持设备", 470), ("region", "来源", 90)]:
            self.source_tree.heading(name, text=title)
            self.source_tree.column(name, width=width, minwidth=70)
        self.source_tree.pack(fill="both", expand=True, pady=12)
        self.source_tree.bind("<Double-1>", lambda event: self.open_source())
        ttk.Button(page, text="打开选中厂商下载页", command=self.open_source).pack(anchor="w")
        ttk.Label(page, text="在厂商页面选择型号后下载。获得 HTTPS 安装包直链后，可切换到“下载文件”保存到电脑。",
                  wraplength=900).pack(anchor="w", pady=8)
        self.filter_sources()

    def filter_sources(self):
        self.source_tree.delete(*self.source_tree.get_children())
        words = self.search.get().strip().lower().split()
        for index, source in enumerate(self.sources):
            text = " ".join(source).lower()
            if all(word in text for word in words):
                self.source_tree.insert("", "end", iid=str(index),
                                        values=(source[0] + " / " + source[1], source[2], source[3], source[5]))

    def reset_sources(self):
        self.search.set("")
        self.filter_sources()

    def open_source(self):
        selected = self.source_tree.selection()
        if selected:
            webbrowser.open(self.sources[int(selected[0])][4])
        else:
            messagebox.showinfo("选择来源", "请先选择一家厂商。")

    def make_hardware(self):
        page = ttk.Frame(self.tabs, padding=12)
        self.tabs.add(page, text="本机硬件")
        row = ttk.Frame(page)
        row.pack(fill="x")
        self.scan_button = ttk.Button(row, text="检测本机硬件", command=self.scan)
        self.scan_button.pack(side="left")
        ttk.Button(row, text="导入设备清单", command=self.import_devices).pack(side="left", padx=8)
        self.hardware_status = tk.StringVar(value="检测功能适用于 Windows，也可以导入 JSON 设备清单。")
        ttk.Label(page, textvariable=self.hardware_status, wraplength=930).pack(anchor="w", pady=10)
        self.hardware_tree = ttk.Treeview(page, columns=("name", "id"), show="headings")
        self.hardware_tree.heading("name", text="设备")
        self.hardware_tree.heading("id", text="硬件 ID")
        self.hardware_tree.column("name", width=340)
        self.hardware_tree.column("id", width=550)
        scroll = ttk.Scrollbar(page, orient="vertical", command=self.hardware_tree.yview)
        self.hardware_tree.configure(yscrollcommand=scroll.set)
        scroll.pack(side="right", fill="y")
        self.hardware_tree.pack(fill="both", expand=True)
        row = ttk.Frame(page)
        row.pack(fill="x", pady=(12, 0))
        ttk.Button(row, text="复制硬件 ID", command=self.copy_id).pack(side="left")
        ttk.Button(row, text="在微软更新目录查找", command=self.search_catalog).pack(side="left", padx=8)

    def scan(self):
        if platform.system() != "Windows":
            messagebox.showinfo("Windows 检测", "请在 Windows 电脑运行，或导入 scan-drivers.ps1 生成的清单。")
            return
        if self.scanning:
            return
        self.scanning = True
        self.scan_button.configure(state="disabled")
        self.hardware_status.set("正在读取硬件信息…")
        threading.Thread(target=self.scan_worker, daemon=True).start()

    def scan_worker(self):
        try:
            result = subprocess.run(
                ["powershell.exe", "-NoProfile", "-NonInteractive", "-Command", SCAN_COMMAND],
                capture_output=True, timeout=60, creationflags=subprocess.CREATE_NO_WINDOW,
            )
            if result.returncode:
                raise ValueError("Windows 硬件查询失败，请确认系统 WMI 服务可用。")
            data = json.loads(result.stdout.decode("utf-8-sig"))
            self.events.put(("scan", data))
        except Exception as error:
            self.events.put(("scan_error", str(error)))

    def import_devices(self):
        path = filedialog.askopenfilename(filetypes=[("设备清单", "*.json")])
        if not path:
            return
        try:
            if Path(path).stat().st_size > 4 * 1024 * 1024:
                raise ValueError("设备清单超过 4 MB。")
            self.show_devices(json.loads(Path(path).read_text(encoding="utf-8-sig")))
        except Exception as error:
            messagebox.showerror("导入失败", str(error))

    def show_devices(self, data):
        devices = validate_report(data)
        self.hardware_tree.delete(*self.hardware_tree.get_children())
        self.devices = devices
        for index, (name, ids) in enumerate(devices):
            self.hardware_tree.insert("", "end", iid=str(index), values=(name, ids[0]))
        computer = data.get("computer") or {}
        summary = " · ".join(str(computer.get(key) or "") for key in ("manufacturer", "model", "os", "architecture"))
        self.hardware_status.set(f"读取到 {len(devices)} 个设备。{summary}")

    def selected_id(self):
        selected = self.hardware_tree.selection()
        if not selected:
            messagebox.showinfo("选择设备", "请先选择一个设备。")
            return None
        return self.devices[int(selected[0])][1][0]

    def copy_id(self):
        hardware_id = self.selected_id()
        if hardware_id:
            self.root.clipboard_clear()
            self.root.clipboard_append(hardware_id)

    def search_catalog(self):
        hardware_id = self.selected_id()
        if hardware_id:
            webbrowser.open("https://catalog.update.microsoft.com/Search.aspx?" + urlencode({"q": hardware_id}))

    def make_downloads(self):
        page = ttk.Frame(self.tabs, padding=18)
        self.tabs.add(page, text="下载文件")
        ttk.Label(page, text="安装包 HTTPS 下载链接").pack(anchor="w")
        self.url = tk.StringVar()
        ttk.Entry(page, textvariable=self.url).pack(fill="x", pady=(6, 18))
        ttk.Label(page, text="在官方页面复制文件的下载链接后粘贴到此处。需要登录或浏览器验证的下载，请在厂商页面完成。",
                  wraplength=850).pack(anchor="w")
        row = ttk.Frame(page)
        row.pack(anchor="w", pady=18)
        self.start_button = ttk.Button(row, text="选择保存位置并下载", command=self.start_download)
        self.start_button.pack(side="left")
        self.cancel_button = ttk.Button(row, text="取消下载", state="disabled", command=self.cancel_download)
        self.cancel_button.pack(side="left", padx=8)
        self.progress = ttk.Progressbar(page, maximum=100)
        self.progress.pack(fill="x", pady=8)
        self.download_status = tk.StringVar(value="等待下载")
        ttk.Label(page, textvariable=self.download_status, wraplength=880).pack(anchor="w")
        self.saved = None
        ttk.Button(page, text="打开下载文件夹", command=self.open_folder).pack(anchor="w", pady=16)

    def start_download(self):
        if self.worker and self.worker.is_alive():
            return
        url = self.url.get().strip()
        try:
            validate_url(url)
        except ValueError as error:
            messagebox.showerror("链接不正确", str(error))
            return
        destination = filedialog.asksaveasfilename(initialfile=suggested_name(url), title="保存驱动文件")
        if not destination:
            return
        self.cancel.clear()
        self.progress.stop()
        self.progress.configure(mode="determinate", value=0)
        self.start_button.configure(state="disabled")
        self.cancel_button.configure(state="normal")
        self.download_status.set("正在连接下载服务器…")
        self.worker = threading.Thread(target=self.download_worker, args=(url, destination), daemon=True)
        self.worker.start()

    def download_worker(self, url, destination):
        try:
            result = download(url, destination, self.cancel,
                              lambda done, total: self.events.put(("progress", (done, total))))
            self.events.put(("complete", str(result)))
        except Cancelled:
            self.events.put(("download_error", "下载已取消。"))
        except Exception as error:
            self.events.put(("download_error", "下载失败，可重新尝试。 " + str(error)))

    def cancel_download(self):
        self.cancel.set()
        self.cancel_button.configure(state="disabled")
        self.download_status.set("正在取消，等待当前网络请求结束…")

    def open_folder(self):
        if self.saved:
            webbrowser.open(Path(self.saved).parent.as_uri())

    def poll(self):
        try:
            while True:
                kind, data = self.events.get_nowait()
                self.handle_event(kind, data)
        except queue.Empty:
            pass
        if self.closing and not (self.worker and self.worker.is_alive()):
            self.root.destroy()
            return
        self.root.after(100, self.poll)

    def handle_event(self, kind, data):
        if kind in ("scan", "scan_error"):
            self.scanning = False
            self.scan_button.configure(state="normal")
            try:
                if kind == "scan_error":
                    raise ValueError(data)
                self.show_devices(data)
            except Exception as error:
                self.hardware_status.set("检测失败。 " + str(error))
        elif kind == "progress":
            done, total = data
            if total:
                self.progress.stop()
                self.progress.configure(mode="determinate", value=done / total * 100)
                self.download_status.set(f"已下载 {done / 1048576:.1f} / {total / 1048576:.1f} MB")
            else:
                if str(self.progress["mode"]) != "indeterminate":
                    self.progress.configure(mode="indeterminate")
                    self.progress.start()
                self.download_status.set(f"已下载 {done / 1048576:.1f} MB，服务器未提供文件大小")
        elif kind in ("complete", "download_error"):
            self.progress.stop()
            self.start_button.configure(state="normal")
            self.cancel_button.configure(state="disabled")
            if kind == "complete":
                self.saved = data
                self.progress.configure(mode="determinate", value=100)
                self.download_status.set("下载完成，已保存到 " + data)
            else:
                self.download_status.set(data)

    def close(self):
        self.closing = True
        self.cancel.set()
        self.root.title("驱动源 · 正在结束下载…")
        self.start_button.configure(state="disabled")
        self.scan_button.configure(state="disabled")


if __name__ == "__main__":
    Application(tk.Tk())
    tk.mainloop()
