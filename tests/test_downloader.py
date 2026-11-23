"""Exercise downloads against a local HTTPS server with a trusted test certificate."""
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import ssl
import subprocess
import tempfile
import threading
import unittest

from downloader import Cancelled, download, validate_url


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def do_GET(self):
        if self.path == "/redirect":
            self.send_response(302)
            self.send_header("Location", "/file")
            self.end_headers()
            return
        if self.path == "/downgrade":
            self.send_response(302)
            self.send_header("Location", "http://localhost/file")
            self.end_headers()
            return
        self.send_response(200)
        self.send_header("Content-Type", "text/html" if self.path == "/html" else "application/octet-stream")
        self.send_header("Content-Length", "200000" if self.path == "/short" else str(len(self.server.payload)))
        self.end_headers()
        try:
            self.wfile.write(self.server.payload)
        except (ConnectionResetError, BrokenPipeError, ssl.SSLEOFError):
            pass  # Cancellation and rejected responses intentionally close the connection.


class DownloadTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        root = Path(cls.temp.name)
        cert, key = root / "cert.pem", root / "key.pem"
        subprocess.run([
            "openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes",
            "-keyout", str(key), "-out", str(cert), "-days", "1",
            "-subj", "/CN=localhost", "-addext", "subjectAltName=DNS:localhost",
        ], check=True, capture_output=True)
        server_context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        server_context.load_cert_chain(cert, key)
        cls.context = ssl.create_default_context(cafile=str(cert))
        cls.server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        cls.server.payload = b"driver-fixture" * 9000
        cls.server.socket = server_context.wrap_socket(cls.server.socket, server_side=True)
        cls.thread = threading.Thread(target=cls.server.serve_forever)
        cls.thread.start()
        cls.url = f"https://localhost:{cls.server.server_port}"

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown()
        cls.thread.join()
        cls.server.server_close()
        cls.temp.cleanup()

    def setUp(self):
        self.work = tempfile.TemporaryDirectory()
        self.addCleanup(self.work.cleanup)
        self.target = Path(self.work.name) / "driver.bin"
        self.target.write_bytes(b"existing")
        self.cancel = threading.Event()

    def run_download(self, path, progress=lambda done, total: None, context=None):
        return download(self.url + path, self.target, self.cancel, progress, context or self.context)

    def assert_preserved(self):
        self.assertEqual(self.target.read_bytes(), b"existing")
        self.assertEqual(list(Path(self.work.name).glob("*.part")), [])

    def test_complete_and_https_redirect(self):
        self.run_download("/redirect")
        self.assertEqual(self.target.read_bytes(), self.server.payload)

    def test_cancel_preserves_previous_file(self):
        self.cancel.set()
        with self.assertRaises(Cancelled):
            self.run_download("/file")
        self.assert_preserved()

    def test_cancel_during_transfer(self):
        with self.assertRaises(Cancelled):
            self.run_download("/file", lambda done, total: self.cancel.set())
        self.assert_preserved()

    def test_incomplete_response_preserves_previous_file(self):
        with self.assertRaises(ValueError):
            self.run_download("/short")
        self.assert_preserved()

    def test_html_is_rejected(self):
        with self.assertRaises(ValueError):
            self.run_download("/html")
        self.assert_preserved()

    def test_insecure_redirect_is_rejected(self):
        with self.assertRaises(ValueError):
            self.run_download("/downgrade")
        self.assert_preserved()

    def test_untrusted_certificate_is_rejected(self):
        from urllib.error import URLError
        with self.assertRaises(URLError):
            self.run_download("/file", context=ssl.create_default_context())
        self.assert_preserved()

    def test_invalid_urls(self):
        for url in ("http://example.com/file", "file:///etc/hosts", "https://user:password@example.com/"):
            with self.subTest(url=url), self.assertRaises(ValueError):
                validate_url(url)


if __name__ == "__main__":
    unittest.main()
