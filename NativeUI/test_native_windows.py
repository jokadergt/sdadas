"""Hosted-runner smoke check for the real native window and every tab."""
import ctypes as c
from ctypes import wintypes as w
import subprocess
import time


def check_native(executable):
    api = c.windll.user32
    callback = c.WINFUNCTYPE(w.BOOL, w.HWND, w.LPARAM)
    api.SendMessageW.argtypes = [w.HWND, w.UINT, w.WPARAM, w.LPARAM]
    api.SendMessageW.restype = c.c_ssize_t
    api.GetWindowTextW.argtypes = [w.HWND, w.LPWSTR, c.c_int]
    api.GetClassNameW.argtypes = [w.HWND, w.LPWSTR, c.c_int]
    api.GetWindowThreadProcessId.argtypes = [w.HWND, c.POINTER(w.DWORD)]
    api.EnumChildWindows.argtypes = [w.HWND, callback, w.LPARAM]
    api.EnumWindows.argtypes = [callback, w.LPARAM]
    process = subprocess.Popen([str(executable)])

    def windows(parent=None):
        found = []
        @callback
        def visit(h, _):
            found.append(h)
            return True
        if parent is None:
            api.EnumWindows(visit, 0)
        else:
            api.EnumChildWindows(parent, visit, 0)
        return found

    def label(h, klass=False):
        b = c.create_unicode_buffer(512)
        (api.GetClassNameW if klass else api.GetWindowTextW)(h, b, 512)
        return b.value

    def wait_for(fn):
        end = time.monotonic() + 15
        while time.monotonic() < end:
            assert process.poll() is None, "Native window exited unexpectedly"
            result = fn()
            if result:
                return result
            time.sleep(.1)
        raise AssertionError("Native window did not become ready")

    try:
        def find_main():
            for h in windows():
                pid = w.DWORD()
                api.GetWindowThreadProcessId(h,c.byref(pid))
                if pid.value == process.pid and label(h,True) == "CaybTrapCpp":
                    return h
        main = wait_for(find_main)
        nav = wait_for(lambda: next((h for h in windows(main) if label(h,True).lower()=="listbox"),None))
        wait_for(lambda: api.SendMessageW(nav,0x18B,0,0)==9)  # LB_GETCOUNT
        for index,title in enumerate(["Integrações","Comportamento","Modificações","Cursores","FastFlags","Aparência","Atalhos","cayb trap","Aplicativo"]):
            api.SendMessageW(nav,0x186,index,0)  # LB_SETCURSEL
            api.SendMessageW(main,0x111,(1<<16)|1,nav)
            wait_for(lambda: title in [label(h) for h in windows(main)])
            time.sleep(.25)  # allow the navigation timer to finish
        api.SendMessageW(main,0x10,0,0)
        assert process.wait(timeout=15)==0
        print("Native window and all nine tab navigation checks passed.")
    finally:
        if process.poll() is None:
            process.terminate()
            process.wait(timeout=15)
