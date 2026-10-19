"""Download and patch only the hash-pinned build copy; no SDK source is vendored."""
import hashlib, pathlib, sys, urllib.request, zipfile
dest=pathlib.Path(sys.argv[1]); dest.parent.mkdir(parents=True,exist_ok=True)
archive=pathlib.Path(sys.argv[2]) if len(sys.argv)>2 and sys.argv[2] else dest.parent/'MAC_1330_SDK.zip'
sha='9f74ffc0300d0b664e0530186d543e060f50e8776ae9325ec7b3f8fb1eb3f230'
if not archive.exists():
    req=urllib.request.Request('https://monkeysaudio.com/files/MAC_1330_SDK.zip',headers={
        'User-Agent':'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 Chrome/131.0.0.0 Safari/537.36',
        'Referer':'https://www.monkeysaudio.com/developers.html'})
    with urllib.request.urlopen(req,timeout=120) as r: data=r.read()
    if hashlib.sha256(data).hexdigest()!=sha: raise RuntimeError('SDK SHA256 mismatch')
    archive.write_bytes(data)
if hashlib.sha256(archive.read_bytes()).hexdigest()!=sha: raise RuntimeError('SDK SHA256 mismatch')
# Extract afresh so source patches never accumulate across reconfiguration.
with zipfile.ZipFile(archive) as z:
    for name in z.namelist():
        if not (dest/name).resolve().is_relative_to(dest.resolve()): raise RuntimeError('Unsafe archive path')
    z.extractall(dest)
def read_source(path):
    # The official 13.30 ZIP uses CRCRLF in these source files. Normalize it
    # before matching a patch so universal-newline conversion cannot add blanks.
    return path.read_bytes().decode('utf-8').replace('\r\r\n','\n').replace('\r\n','\n').replace('\r','\n')
for name in ['Source/Shared/All.h','Shared/All.h']:
    p=dest/name;s=read_source(p)
    old_export='#define DLLEXPORT                                   __declspec(dllexport)'
    if s.count(old_export)!=1: raise RuntimeError('Static export patch no longer applies')
    s=s.replace(old_export,'#define DLLEXPORT /* static core, plugin exports are controlled by .def */')
    s=s.replace('#define WIN32_LEAN_AND_MEAN','#ifndef WIN32_LEAN_AND_MEAN\n    #define WIN32_LEAN_AND_MEAN\n    #endif')
    p.write_text(s)
p=dest/'Source/MACLib/APECompress.cpp';s=read_source(p)
old='    m_spAPECompressCreate->Start(m_spioOutput, m_nThreads, pwfeInput, nMaxAudioBytes, nCompressionLevel,\n        pHeaderData, nHeaderBytes);'
if s.count(old)!=1: raise RuntimeError('StartEx patch no longer applies')
s=s.replace(old,'    const int nStartResult = m_spAPECompressCreate->Start(m_spioOutput, m_nThreads, pwfeInput, nMaxAudioBytes, nCompressionLevel,\n        pHeaderData, nHeaderBytes);\n    if (nStartResult != ERROR_SUCCESS) return nStartResult;')
p.write_text(s)
