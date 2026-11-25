"""Rebuild the OEM model index from pinned, local catalog snapshots.
No network access or installation. The C build and application do not need Python.
"""
from datetime import datetime
import hashlib
import json
from pathlib import Path
import re
import xml.etree.ElementTree as ET

BASE = Path(__file__).resolve().parents[1]
SOURCE_COMMIT = '1bf395f2b673b2874992716a776c0be4a8c98c6e'


def build():
    rows = []
    folder = BASE / 'database/oem-snapshots'
    for model in ET.parse(folder / 'lenovo.xml').getroot().findall('Model'):
        if 'ARM' in model.get('arch', '').upper():
            continue
        ids = sorted({s.strip().upper() for n in model.findall('Types/Type') for s in (n.text or '').split(',') if re.fullmatch('[A-Za-z0-9]{4}', s.strip())})
        if not ids:
            continue
        for pack in model.findall('SCCM'):
            os = pack.get('os', '')
            release = pack.get('version', '')
            if os not in ('win10', 'win11') or (os == 'win10' and release.isdigit() and int(release) < 1809):
                continue
            url = (pack.text or '').strip()
            sha = pack.get('crc', '').lower()
            if not re.fullmatch('[0-9a-f]{64}', sha) or not url.startswith('https://download.lenovo.com/') or not url.lower().endswith('.exe'):
                continue
            if not re.fullmatch(r'\d{4}-\d{2}-\d{2}', pack.get('date', '')):
                continue
            rows.append(['lenovo', model.get('name'), ';'.join(ids), os, release, pack.get('date'), url, sha, '0', 'exe-download'])
    ns = {'d': 'openmanage/cm/dm'}
    root = ET.parse(folder / 'dell.xml').getroot()
    for pack in root.findall('d:DriverPackage', ns):
        if pack.get('type') != 'win' or pack.get('format') not in ('cab', 'exe'):
            continue
        sha = pack.find("d:Cryptography/d:Hash[@algorithm='SHA256']", ns)
        if sha is None or not re.fullmatch('[0-9a-fA-F]{64}', (sha.text or '').strip()):
            continue
        path = pack.get('path', '')
        if not re.fullmatch(r'[A-Za-z0-9_./-]+', path) or '..' in path.split('/'):
            continue
        for os in pack.findall('d:SupportedOperatingSystems/d:OperatingSystem', ns):
            family = {'Windows10': 'win10', 'Windows11': 'win11'}.get(os.get('osCode'))
            if not family or os.get('osArch') != 'x64':
                continue
            for model in pack.findall('d:SupportedSystems/d:Brand/d:Model', ns):
                ids = model.get('systemID', '').upper()
                if not re.fullmatch('[0-9A-F]{4}', ids):
                    continue
                rows.append(['dell', model.get('name'), ids, family, '*', pack.get('dateTime', '')[:10], 'https://downloads.dell.com/' + path, sha.text.strip().lower(), pack.get('size'), 'cab' if pack.get('format') == 'cab' else 'exe-download'])
    # HP SystemId describes the baseboard ID, not the marketing System SKU.
    hp = ET.parse(folder / 'hp.xml').getroot().find('HPClientDriverPackCatalog')
    softpaqs = {p.findtext('Id'): p for p in hp.findall('SoftPaqList/SoftPaq')}
    for product in hp.findall('ProductOSDriverPackList/ProductOSDriverPack'):
        os_name = product.findtext('OSName', '')
        os_match = re.fullmatch(r'Windows (10|11) 64-bit, ([0-9]{4}|2[0-9]H[12])', os_name)
        if product.findtext('Architecture') != '64-bit' or not os_match:
            continue
        family = 'win' + os_match[1]
        release = os_match[2]
        if family == 'win10' and release.isdigit() and int(release) < 1809:
            continue
        pack = softpaqs.get(product.findtext('SoftPaqId'))
        if pack is None or pack.findtext("Category") != "Manageability - Driver Pack":
            continue
        sha = pack.findtext('SHA256', '').lower()
        url = pack.findtext('Url', '')
        ids = sorted({i.strip().upper() for i in product.findtext('SystemId', '').split(',') if re.fullmatch('[0-9A-Fa-f]{4}', i.strip())})
        if not ids or not re.fullmatch('[0-9a-f]{64}', sha) or not url.startswith('https://ftp.hp.com/pub/softpaq/') or not url.lower().endswith('.exe'):
            continue
        size = pack.findtext('Size', '')
        if not size.isdigit() or not 0 < int(size) <= 17179869184:
            continue
        date = datetime.strptime(pack.findtext('DateReleased').split()[0], '%m/%d/%Y').strftime('%Y-%m-%d')
        rows.append(['hp', product.findtext('SystemName'), ';'.join(ids), family, release, date, url, sha, size, 'exe-download'])
    # Retain aliases and OS versions, remove duplicate model/package records.
    rows = sorted({tuple(r) for r in rows})
    output = ['# key\tmanufacturer\tmodel\ttype_or_system_ids\tos_family\tos_release\tdate\thttps_url\tsha256\tbytes_or_0_unknown\tformat']
    for i, row in enumerate(rows):
        key = f'oem-{row[0]}-{i:04d}'
        assert all(value and '\t' not in value and '\n' not in value and '\r' not in value for value in row)
        output.append('\t'.join([key, *row]))
    (BASE / 'database/oem-packages.tsv').write_text('\n'.join(output) + '\n', encoding='utf-8')
    provenance = {
        'upstream_repository': 'https://github.com/OSDeploy/OSD',
        'upstream_commit': SOURCE_COMMIT,
        'license': 'GPL-3.0, data files and generated index; see LICENSE-OSD-GPL-3.0.txt',
        'snapshot_files': {name: hashlib.sha256((folder / name).read_bytes()).hexdigest() for name in ['lenovo.xml', 'dell.xml', 'hp.xml']},
        'original_catalogs': ['https://download.lenovo.com/cdrt/td/catalogv2.xml', 'https://downloads.dell.com/catalog/DriverPackCatalog.cab', 'https://ftp.hp.com/pub/caps-softpaq/cmit/HPClientDriverPackCatalog.cab'],
        'entries': len(rows),
        'lenovo_models': len({r[1] for r in rows if r[0] == 'lenovo'}),
        'dell_models': len({r[1] for r in rows if r[0] == 'dell'}),
        'hp_models': len({r[1] for r in rows if r[0] == 'hp'}),
        'payload_validation': 'Catalog metadata and SHA-256 are indexed. Vendor payload URLs are blocked by the development cloud egress policy. Downloads must pass pinned SHA-256 on the user computer. EXE packs support download only.'
    }
    (folder / 'provenance.json').write_text(json.dumps(provenance, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({k: provenance[k] for k in ('entries', 'lenovo_models', 'dell_models', 'hp_models')}))


if __name__ == '__main__':
    build()
