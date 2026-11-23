"""HTTPS file downloads with atomic completion and cancellable progress."""
from pathlib import Path
import os
import tempfile
import time
from urllib.parse import urlsplit
from urllib.request import HTTPRedirectHandler, Request, build_opener, HTTPSHandler


class Cancelled(Exception):
    pass


def validate_url(url):
    parsed = urlsplit(url)
    if parsed.scheme.lower() != "https" or not parsed.hostname or parsed.username or parsed.password:
        raise ValueError("请输入无需登录的 HTTPS 文件直链。")
    return url


class SecureRedirects(HTTPRedirectHandler):
    def redirect_request(self, request, fp, code, message, headers, new_url):
        validate_url(new_url)
        return super().redirect_request(request, fp, code, message, headers, new_url)


def download(url, destination, cancel, progress, context=None):
    """Keep the destination intact until a complete response is received."""
    validate_url(url)
    destination = Path(destination)
    opener = build_opener(SecureRedirects(), HTTPSHandler(context=context))
    request = Request(url, headers={"User-Agent": "DriverSource/0.1.0", "Accept-Encoding": "identity"})
    temporary = None
    received = 0
    try:
        with opener.open(request, timeout=20) as response:
            if response.status != 200:
                raise ValueError("服务器没有返回完整文件，请使用最终下载地址。")
            if response.headers.get_content_type() in ("text/html", "application/xhtml+xml"):
                raise ValueError("该地址返回了网页，请在厂商页面复制安装包的下载链接。")
            length = response.headers.get("Content-Length")
            total = int(length) if length is not None else None
            descriptor, temporary = tempfile.mkstemp(prefix=".driver-source-", suffix=".part", dir=destination.parent)
            with os.fdopen(descriptor, "wb") as output:
                last_update = 0
                while True:
                    if cancel.is_set():
                        raise Cancelled()
                    chunk = response.read1(64 * 1024)
                    if not chunk:
                        break
                    output.write(chunk)
                    received += len(chunk)
                    now = time.monotonic()
                    if now - last_update > 0.15:
                        progress(received, total)
                        last_update = now
                if cancel.is_set():
                    raise Cancelled()
                if received == 0:
                    raise ValueError("服务器返回了空文件。")
                if total is not None and received != total:
                    raise ValueError("下载未完成，文件长度与服务器声明不符。")
                output.flush()
                os.fsync(output.fileno())
            os.replace(temporary, destination)
            temporary = None
            progress(received, total)
        return destination
    finally:
        if temporary is not None:
            Path(temporary).unlink(missing_ok=True)
