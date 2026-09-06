"""Build offline ScrcpyKeyMapper assets from a pinned submodule/npm archives.
Only generated build-directory copies are adapted; upstream stays unmodified.
"""
from pathlib import Path
import argparse, hashlib, shutil, tarfile, urllib.request, re
import xml.etree.ElementTree as ET

PIN = '022870e2caf905046c8e7502d4053bb5f171c1e4'
PACKAGES = {
    'konva-9.2.3.tgz': ('https://registry.npmjs.org/konva/-/konva-9.2.3.tgz', '7bf2e5c6c9e01727946d0c001699c16d7f9f4780770e064624514a7fc17a13e4', {'package/konva.min.js':'vendor/konva.min.js', 'package/LICENSE':'licenses/Konva.txt'}),
    'bootstrap-5.3.3.tgz': ('https://registry.npmjs.org/bootstrap/-/bootstrap-5.3.3.tgz', '38cee936dbd80138de6775683149f22e9226fc2d654392337a921f53000c789e', {'package/dist/css/bootstrap.min.css':'vendor/bootstrap.min.css', 'package/dist/js/bootstrap.bundle.min.js':'vendor/bootstrap.bundle.min.js', 'package/LICENSE':'licenses/Bootstrap.txt'})
}

def prepare(root: Path, output: Path, cache: Path, offline: bool = False):
    source = root / 'QtScrcpy/third_party/ScrcpyKeyMapper'
    if not (source / 'js/app.js').is_file():
        raise RuntimeError('Initialize the pinned ScrcpyKeyMapper submodule first.')
    output.mkdir(parents=True, exist_ok=True)
    cache.mkdir(parents=True, exist_ok=True)
    for directory in ('js', 'css'):
        shutil.copytree(source / directory, output / directory, dirs_exist_ok=True)
    for name in ('index.html', 'LICENSE', 'assets/favicon.svg'):
        target=output/name; target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source/name,target)
    for name,(url,expected,selected) in PACKAGES.items():
        archive=cache/name
        if not archive.exists():
            if offline: raise RuntimeError(f'Missing offline dependency: {name}')
            with urllib.request.urlopen(url,timeout=60) as response:
                data=response.read(16*1024*1024+1)
            if len(data)>16*1024*1024: raise RuntimeError('Dependency size limit exceeded')
            if hashlib.sha256(data).hexdigest()!=expected: raise RuntimeError(f'Dependency digest mismatch: {name}')
            archive.write_bytes(data)
        if hashlib.sha256(archive.read_bytes()).hexdigest()!=expected: raise RuntimeError(f'Dependency digest mismatch: {name}')
        with tarfile.open(archive,'r:gz') as tar:
            for member,relative in selected.items():
                entry=tar.getmember(member)
                if not entry.isfile() or entry.size>8*1024*1024: raise RuntimeError('Invalid dependency member')
                data=tar.extractfile(entry).read()
                target=output/relative; target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(data)
    path=output/'index.html';html=path.read_text(encoding='utf-8')
    html=re.sub(r'    <link href="https://cdn.jsdelivr.net/npm/bootstrap-icons[^\n]*\n','',html)
    html=re.sub(r'    <link href="https://cdn.jsdelivr.net/gh/lipis/flag-icons[^\n]*\n','',html)
    html=html.replace('https://cdn.jsdelivr.net/npm/bootstrap@5.3.3/dist/css/bootstrap.min.css','vendor/bootstrap.min.css')
    html=html.replace('https://cdn.jsdelivr.net/npm/bootstrap@5.3.3/dist/js/bootstrap.bundle.min.js','vendor/bootstrap.bundle.min.js')
    html=html.replace('https://unpkg.com/konva@9.2.3/konva.min.js','vendor/konva.min.js')
    html=html.replace('src="js/app.js"','src="adapter.js"').replace('<html lang="en"','<html lang="zh"')
    html=html.replace('</head>','<meta http-equiv="Content-Security-Policy" content="default-src \'none\'; script-src \'self\' qrc:; style-src \'self\' \'unsafe-inline\'; img-src \'self\' data: blob:; connect-src \'none\'; object-src \'none\'; base-uri \'none\'; form-action \'none\'">\n<link href="integration.css" rel="stylesheet">\n</head>')
    html=html.replace('<script type="module"','<script src="qrc:///qtwebchannel/qwebchannel.js"></script>\n    <script type="module"')
    path.write_text(html,encoding='utf-8',newline='\n')
    path=output/'js/app.js';app=path.read_text(encoding='utf-8')
    tail="document.addEventListener('DOMContentLoaded', () => {\n    new App();\n});"
    if app.count(tail)!=1 or app.count('class App {')!=1: raise RuntimeError('Unexpected pinned App baseline')
    app=app.replace('class App {','export class App {',1).replace(tail,'')
    path.write_text(app,encoding='utf-8',newline='\n')
    for name in ('adapter.js','integration.css'):
        shutil.copyfile(root/'QtScrcpy/keymapper/web'/name,output/name)
    (output/'UPSTREAM.txt').write_text(f'ScrcpyKeyMapper by w4po (MIT)\nUpstream: https://github.com/w4po/ScrcpyKeyMapper\nPinned commit: {PIN}\nGenerated adaptations: local assets, native bridge, configuration round trip and side-button capture.\n',encoding='utf-8')
    resource=ET.Element('RCC');section=ET.SubElement(resource,'qresource',prefix='/keymapper')
    for path in sorted(output.rglob('*')):
        if path.is_file() and path.name!='keymapper_assets.qrc':
            ET.SubElement(section,'file',alias=path.relative_to(output).as_posix()).text=str(path.resolve())
    ET.ElementTree(resource).write(output/'keymapper_assets.qrc',encoding='utf-8',xml_declaration=True)

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--root',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--cache',type=Path,required=True);p.add_argument('--offline',action='store_true')
    a=p.parse_args();prepare(a.root.resolve(),a.output.resolve(),a.cache.resolve(),a.offline)
