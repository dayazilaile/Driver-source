"""Optional maintenance check. Run from the repository root; never installs drivers.
The C app and its build do not require Python. Download hosts must be allowed by
this environment's network policy. The tool does not modify the pinned catalog.
"""
import csv
import hashlib
import io
import re
import urllib.request
import zipfile
from pathlib import Path


def resolve_inf_ids(text):
    strings = dict(re.findall(r'^\s*([\w]+)\s*=\s*"([^"\r\n]*)"', text, re.M))
    for key, value in strings.items():
        text = re.sub('%' + re.escape(key) + '%', lambda _: value, text, flags=re.I)
    return {i.upper() for i in re.findall(r'(?:USB\\VID_[0-9A-F]{4}&PID_[0-9A-F]{4}|PCI\\VEN_[0-9A-F]{4}&DEV_[0-9A-F]{4})(?:&[A-Z0-9_]+)*', text, re.I)}


def main():
    with Path('database/vendor-packages.tsv').open(encoding='utf-8', newline='') as file:
        rows = list(csv.reader((line for line in file if line.strip() and not line.startswith('#')), delimiter='\t'))
    for row in rows:
        key, _, _, _, ids, url, sha256, size, _, arch, inf = row
        assert arch == 'x64' and url.startswith('https://')
        with urllib.request.urlopen(url, timeout=30) as response:
            data = response.read(int(size) + 1)
            assert response.url.startswith('https://')
        assert len(data) == int(size), key + ': size changed'
        assert hashlib.sha256(data).hexdigest() == sha256, key + ': SHA-256 changed'
        with zipfile.ZipFile(io.BytesIO(data)) as archive:
            assert archive.testzip() is None
            for item in archive.infolist():
                path = item.filename.replace('\\', '/')
                assert not path.startswith('/') and '..' not in path.split('/') and ':' not in path
                assert (item.external_attr >> 16) & 0o170000 != 0o120000, 'symlink'
            raw = archive.read(inf)
            text = raw.decode('utf-16' if raw.startswith((b'\xff\xfe', b'\xfe\xff')) else 'utf-8-sig')
            assert '.NTamd64]' in text
            declared = {value.upper() for value in ids.split(';')}
            assert declared <= resolve_inf_ids(text), key + ': hardware IDs absent from INF'
            catalog_name = re.search(r'^CatalogFile\s*=\s*(\S+)', text, re.M | re.I)
            assert catalog_name and archive.read(catalog_name[1])
        print(f'{key}: size, SHA-256, x64 INF IDs and catalog verified')
    print(f'{len(rows)} official packages checked. Installation and domestic speed require Windows/device testing.')


if __name__ == '__main__':
    main()
