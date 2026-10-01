"""Check that the compiled PE embeds the current image bytes, not stale assets."""
import ctypes
import hashlib
import sys
from ctypes import wintypes
from pathlib import Path

root = Path(__file__).resolve().parents[1]
binary = Path(sys.argv[1]).resolve()
kernel = ctypes.WinDLL("kernel32", use_last_error=True)
kernel.LoadLibraryExW.argtypes = [wintypes.LPCWSTR, wintypes.HANDLE, wintypes.DWORD]
kernel.LoadLibraryExW.restype = wintypes.HMODULE
kernel.FindResourceW.argtypes = [wintypes.HMODULE, wintypes.LPCWSTR, wintypes.LPCWSTR]
kernel.FindResourceW.restype = wintypes.HRSRC
kernel.SizeofResource.argtypes = [wintypes.HMODULE, wintypes.HRSRC]
kernel.SizeofResource.restype = wintypes.DWORD
kernel.LoadResource.argtypes = [wintypes.HMODULE, wintypes.HRSRC]
kernel.LoadResource.restype = wintypes.HGLOBAL
kernel.LockResource.argtypes = [wintypes.HGLOBAL]
kernel.LockResource.restype = wintypes.LPVOID
kernel.FreeLibrary.argtypes = [wintypes.HMODULE]
user32 = ctypes.WinDLL("user32", use_last_error=True)
user32.LoadStringW.argtypes = [wintypes.HMODULE, wintypes.UINT, wintypes.LPWSTR, ctypes.c_int]
user32.LoadStringW.restype = ctypes.c_int

module = kernel.LoadLibraryExW(str(binary), None, 2)
if not module:
    raise OSError(ctypes.get_last_error(), f"LoadLibraryExW failed: {binary}")
try:
    dialog = kernel.FindResourceW(module, ctypes.cast(ctypes.c_void_p(301), wintypes.LPCWSTR),
                                  ctypes.cast(ctypes.c_void_p(5), wintypes.LPCWSTR))
    if not dialog:
        raise RuntimeError("Missing Win32 settings dialog resource")
    dialog_bytes = ctypes.string_at(kernel.LockResource(kernel.LoadResource(module, dialog)),
                                    kernel.SizeofResource(module, dialog))
    if "迷宫设置".encode("utf-16le") not in dialog_bytes:
        raise RuntimeError("Settings dialog caption is missing or incorrectly encoded")
    version = kernel.FindResourceW(module, ctypes.cast(ctypes.c_void_p(1), wintypes.LPCWSTR),
                                   ctypes.cast(ctypes.c_void_p(16), wintypes.LPCWSTR))
    if not version:
        raise RuntimeError("Missing version information resource")
    manifest = kernel.FindResourceW(module, ctypes.cast(ctypes.c_void_p(1), wintypes.LPCWSTR),
                                    ctypes.cast(ctypes.c_void_p(24), wintypes.LPCWSTR))
    if not manifest:
        raise RuntimeError("Missing application manifest")
    manifest_bytes = ctypes.string_at(kernel.LockResource(kernel.LoadResource(module, manifest)),
                                      kernel.SizeofResource(module, manifest))
    if b"Microsoft.Windows.Common-Controls" not in manifest_bytes:
        raise RuntimeError("Common Controls v6 visual style dependency is missing")
    title = ctypes.create_unicode_buffer(128)
    if not user32.LoadStringW(module, 1, title, len(title)) or title.value != "Win95-3Dmaze-Remake":
        raise RuntimeError(f"Incorrect screen saver display name: {title.value!r}")
    print(f"verified dialog and display name: {title.value}")
    for identifier, name in enumerate(
        ("wall.png", "floor.png", "ceiling.png", "pic.png", "rat.png",
         "fin.png", "start2.png", "gl.png", "dx.jpg", "yb.jpg"), 101
    ):
        resource = kernel.FindResourceW(module, ctypes.cast(ctypes.c_void_p(identifier), wintypes.LPCWSTR),
                                        ctypes.cast(ctypes.c_void_p(10), wintypes.LPCWSTR))
        if not resource:
            raise RuntimeError(f"Missing RCDATA resource {identifier}: {name}")
        length = kernel.SizeofResource(module, resource)
        address = kernel.LockResource(kernel.LoadResource(module, resource))
        embedded = ctypes.string_at(address, length)
        source = (root / "assets" / name).read_bytes()
        if hashlib.sha256(embedded).digest() != hashlib.sha256(source).digest():
            raise RuntimeError(f"Stale embedded resource: {name}")
        print(f"verified {name}: {length} bytes")
finally:
    kernel.FreeLibrary(module)
