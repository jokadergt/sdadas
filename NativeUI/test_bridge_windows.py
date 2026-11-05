"""Windows-only bridge integration check. Does not launch/download Roblox."""
import json
import os
import queue
import subprocess
import sys
import tempfile
import threading
from pathlib import Path
import winreg

KEY = r"Software\Microsoft\Windows\CurrentVersion\Uninstall\cayb trap"


def main():
    engine = Path(sys.argv[1]).resolve()
    try:
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, KEY):
            raise RuntimeError("Existing installation; refusing to touch it")
    except FileNotFoundError:
        pass
    process = None
    with tempfile.TemporaryDirectory(prefix="cayb-bridge-test-") as temp:
        try:
            with winreg.CreateKey(winreg.HKEY_CURRENT_USER, KEY) as key:
                winreg.SetValueEx(key, "InstallLocation", 0, winreg.REG_SZ, temp)
            process = subprocess.Popen([str(engine), "-nativebridge"], stdin=subprocess.PIPE,
                                       stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                       text=True, encoding="utf-8", cwd=str(engine.parent))
            replies = queue.Queue()

            def read():
                for line in process.stdout:
                    try:
                        replies.put(json.loads(line))
                    except Exception as error:
                        replies.put(error)

            threading.Thread(target=read, daemon=True).start()

            def receive(ok=True):
                response = replies.get(timeout=60)
                assert isinstance(response, dict), response
                assert response.get("ok") is ok, response
                return response

            def request(data, ok=True):
                process.stdin.write(json.dumps(data) + "\n")
                process.stdin.flush()
                return receive(ok)

            state = receive()
            assert state["installed"]
            pages = {p["id"]: p for p in state["pages"]}
            assert set(pages) == {"Integrations", "Behaviour", "Mods", "FastFlags", "Appearance", "Shortcuts", "Bloxstrap"}
            assert any(f["path"] == "CursorTypeTask.NewState" for f in pages["Mods"]["fields"])
            request({"op": "save", "changes": [
                {"page": "Behaviour", "path": "ConfirmLaunches", "value": True},
                {"page": "Appearance", "path": "Title", "value": "Capybara test"},
                {"page": "Mods", "path": "OldCharacterSoundsTask.NewState", "value": True},
                {"page": "FastFlags", "path": "$flags", "value": {"FFlagCaybBridgeTest": "True"}},
            ]})
            settings = json.loads((Path(temp) / "Settings.json").read_text(encoding="utf-8-sig"))
            assert settings["ConfirmLaunches"] is True
            assert settings["BootstrapperTitle"] == "Capybara test"
            assert (Path(temp) / "Modifications/content/sounds/action_jump.mp3").is_file()
            request({"op": "command", "page": "Mods", "path": "NotACommand"}, ok=False)
            request({"op": "apply", "changes": [
                {"page": "Appearance", "path": "Dialog", "value": "InvalidStyle"}
            ]}, ok=False)
            # External edits must not be overwritten silently.
            path = Path(temp) / "Settings.json"
            settings["ConfirmLaunches"] = False
            path.write_text(json.dumps(settings), encoding="utf-8")
            request({"op": "save", "changes": []}, ok=False)
            assert json.loads(path.read_text())["ConfirmLaunches"] is False
            process.stdin.close()
            assert process.wait(timeout=30) == 0
            print("Bridge integration checks passed.")
        finally:
            if process and process.poll() is None:
                process.terminate()
                process.wait(timeout=15)
            winreg.DeleteKey(winreg.HKEY_CURRENT_USER, KEY)


if __name__ == "__main__":
    if os.name != "nt":
        raise SystemExit("Run on Windows only")
    main()
