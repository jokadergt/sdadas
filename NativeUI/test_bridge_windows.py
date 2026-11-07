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
    checked = subprocess.run([str(engine), "-selftest-cursors"], capture_output=True, text=True, timeout=60)
    assert checked.returncode == 0, checked.stderr
    try:
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, KEY):
            raise RuntimeError("Existing installation; refusing to touch it")
    except FileNotFoundError:
        pass
    process = None
    fake_game = None
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
            request({"op":"cursor.preview", "slug":"../../outside"}, ok=False)
            request({"op":"cursor.search", "query":""}, ok=False)
            # A single bounded public search and the selected pair of Roblox PNGs.
            process.stdin.write(json.dumps({"op":"cursor.search", "query":"minecraft"}) + "\n")
            process.stdin.flush()
            live = replies.get(timeout=60)
            if live.get("ok"):
                catalog = live["cursorCatalog"]
                assert 0 < len(catalog["results"]) <= 24
                slug = catalog["results"][0]["slug"]
                preview = request({"op":"cursor.preview", "slug":slug})
                assert Path(preview["cursorCatalog"]["cursorPreview"]).read_bytes().startswith(b"\x89PNG")
                installed = request({"op":"cursor.install", "slug":slug})
                assert installed["cursorCatalog"]["installedName"]
                assert (Path(temp)/"Modifications/content/textures/Cursors/KeyboardMouse/ArrowCursor.png").is_file()
                request({"op":"cursor.reset"})
                assert not (Path(temp)/"Modifications/content/textures/Cursors/KeyboardMouse/ArrowCursor.png").exists()
                print("Sweezy live search, PNG preview, install and reset passed.")
            else:
                error = live.get("error", "")
                assert any(text in error for text in ["limitou o acesso", "Não foi possível conectar", "demorou para responder", "HTTP 5"]), live
                print("Sweezy live network check unavailable: " + error)

            custom_font = Path(temp) / "Modifications/content/fonts/CustomFont.ttf"
            assert custom_font.is_file()
            import hashlib
            assert hashlib.sha256(custom_font.read_bytes()).hexdigest() == "bd47314d301e50ff4d109bff28dfcf637cb7eb13945480259878b848875acc65"
            assert json.loads((Path(temp) / "State.json").read_text())["MinecraftFontApplied"] is True
            expected_flags = json.loads((Path(__file__).resolve().parents[1] / "Bloxstrap/Resources/AllowedFastFlags.json").read_text())
            flags_path = Path(temp) / "Modifications/ClientSettings/ClientAppSettings.json"
            assert len(expected_flags) == 18
            assert json.loads(flags_path.read_text()) == expected_flags
            pages = {p["id"]: p for p in state["pages"]}
            assert set(pages) == {"Integrations", "Behaviour", "Mods", "Cursors", "FastFlags", "Appearance", "Shortcuts", "Bloxstrap", "Application"}
            assert any(f["path"] == "CursorTypeTask.NewState" for f in pages["Mods"]["fields"])
            request({"op": "save", "changes": [
                {"page": "Behaviour", "path": "BackgroundUpdates", "value": True},
                {"page": "Appearance", "path": "Title", "value": "Capybara test"},
                {"page": "Mods", "path": "OldCharacterSoundsTask.NewState", "value": True},
                {"page": "FastFlags", "path": "$flags", "value": {"FFlagCaybBridgeTest": "True"}},
            ]})
            settings = json.loads((Path(temp) / "Settings.json").read_text(encoding="utf-8-sig"))
            assert settings["BackgroundUpdatesEnabled"] is True
            assert settings["BootstrapperTitle"] == "Capybara test"
            assert json.loads(flags_path.read_text()) == expected_flags, "Unlisted flags were not removed"
            missing = request({"op":"save"})
            assert "não encontrada" in missing["notice"]
            target = Path(temp)/"Versions/version-f0ea5da9412f4e35"
            target.mkdir(parents=True)
            (target/"RobloxPlayerBeta.exe").write_bytes(b"fixture")
            families = target/"content/fonts/families"
            families.mkdir(parents=True)
            family = families/"Test.json"
            family.write_text(json.dumps({"name":"Test","faces":[{"assetId":"original"}]}))
            marker = Path(temp)/"Modifications/target-check.txt"
            marker.write_text("custom")
            (target/"target-check.txt").write_text("original")
            applied = request({"op":"save"})
            assert "Aplicado em version-f0ea5da9412f4e35" in applied["notice"]
            assert json.loads((target/"ClientSettings/ClientAppSettings.json").read_text()) == expected_flags
            assert (target/"content/fonts/CustomFont.ttf").read_bytes() == custom_font.read_bytes()
            assert json.loads(family.read_text())["faces"][0]["assetId"] == "rbxasset://fonts/CustomFont.ttf"
            assert (target/"target-check.txt").read_text() == "custom"
            marker.unlink()
            request({"op":"save"})
            assert (target/"target-check.txt").read_text() == "original"
            marker.write_text("again")
            request({"op":"save"})
            assert (target/"target-check.txt").read_text() == "again"
            marker.unlink()
            request({"op":"save"})
            other = Path(temp)/"Versions/version-1111111111111111"
            other.mkdir(parents=True)
            (other/"RobloxPlayerBeta.exe").write_bytes(b"fixture")
            refreshed = request({"op":"snapshot"})
            application = next(p for p in refreshed["pages"] if p["id"] == "Application")
            version_field = next(f for f in application["fields"] if f["path"] == "InstalledVersion")
            selected_path = next(p for p in version_field["choices"] if p.endswith("version-1111111111111111"))
            selected = request({"op":"save", "changes":[
                {"page":"Application","path":"InstalledVersion","value":selected_path},
                {"page":"Application","path":"AppFont","value":"Consolas"},
                {"page":"Application","path":"Corner","value":"MuitoRedondo"},
                {"page":"Application","path":"ApplicationNotifications","value":False}]})
            assert "Aplicado em version-1111111111111111" in selected["notice"]
            assert json.loads((other/"ClientSettings/ClientAppSettings.json").read_text()) == expected_flags
            assert selected["appFont"] == "Consolas" and selected["appCorner"] == 3
            prefs = json.loads((Path(temp)/"Settings.json").read_text())
            assert Path(prefs["TargetVersionPath"]).samefile(other)
            assert prefs["ApplicationNotifications"] is False
            invalid = request({"op":"save","changes":[{"page":"Application","path":"InstalledVersion","value":str(Path(temp)/"unknown")}]}, ok=False)
            assert not invalid["ok"]
            request({"op":"save","changes":[{"page":"Application","path":"InstalledVersion","value":""}]})
            print("Installed version selection and application preferences checks passed.")
            print("Target version application and restore checks passed.")
            # The graphics profile must persist, enforce its values, and restore the user's prior choices.
            before_profile = json.loads(flags_path.read_text())
            assert settings["LowGraphicsMode"] is False
            enabled = request({"op":"save", "changes":[{"page":"FastFlags", "path":"LowGraphicsMode", "value":True}]})
            minimum = {"DFFlagTextureQualityOverrideEnabled":"True", "DFIntTextureQualityOverride":"0", "FIntDebugForceMSAASamples":"1", "FIntFRMMinGrassDistance":"0", "FIntFRMMaxGrassDistance":"0"}
            actual = json.loads(flags_path.read_text())
            assert all(actual[key] == value for key,value in minimum.items())
            backup = json.loads((Path(temp)/"State.json").read_text())["PerformanceOriginalFlags"]
            assert backup == {key:before_profile.get(key) for key in minimum}
            fast_page = next(page for page in enabled["pages"] if page["id"] == "FastFlags")
            assert next(field for field in fast_page["fields"] if field["path"] == "SelectedTextureQuality")["enabled"] is False
            changed_flags = dict(actual, DFIntTextureQualityOverride="3")
            request({"op":"save", "changes":[{"page":"FastFlags", "path":"$flags", "value":changed_flags}]})
            assert json.loads(flags_path.read_text())["DFIntTextureQualityOverride"] == "0"
            request({"op":"save", "changes":[{"page":"FastFlags", "path":"LowGraphicsMode", "value":False}]})
            assert json.loads(flags_path.read_text()) == before_profile
            assert json.loads((Path(temp)/"State.json").read_text())["PerformanceOriginalFlags"] is None
            print("Reversible graphics profile checks passed.")

            assert settings["BackgroundFileCacheCleaner"] is False
            assert settings["BubbleSounds"] is True
            conflicting = dict(expected_flags, FFlagDebugGraphicsPreferVulkan="True", FFlagDebugGraphicsPreferOpenGL="True")
            request({"op": "save", "changes": [{"page":"FastFlags", "path":"$flags", "value":conflicting}]})
            resolved = json.loads(flags_path.read_text())
            assert sum(resolved[k].lower() == "true" for k in ["FFlagDebugGraphicsPreferD3D11","FFlagDebugGraphicsPreferVulkan","FFlagDebugGraphicsPreferOpenGL"]) == 1
            changed = request({"op": "save", "changes": [
                {"page":"Behaviour", "path":"BackgroundFileCacheCleaner", "value":True},
                {"page":"Bloxstrap", "path":"BubbleSounds", "value":False}]})
            assert changed["bubbleSounds"] is False
            assert json.loads((Path(temp)/"Settings.json").read_text())["BackgroundFileCacheCleaner"] is True
            request({"op":"save", "changes":[{"page":"Behaviour", "path":"BackgroundFileCacheCleaner", "value":False}]})
            settings = json.loads((Path(temp)/"Settings.json").read_text())

            assert (Path(temp) / "Modifications/content/sounds/action_jump.mp3").is_file()
            request({"op": "command", "page": "Mods", "path": "NotACommand"}, ok=False)
            request({"op": "apply", "changes": [
                {"page": "Appearance", "path": "Dialog", "value": "InvalidStyle"}
            ]}, ok=False)
            # Test reuse/autosave with a small owned fixture, never with a real game.
            fake_path = Path(temp) / "RobloxPlayerBeta.exe"
            subprocess.run(["g++", str(Path(__file__).with_name("test_existing_instance.cpp")),
                            "-municode", "-mwindows", "-static", "-o", str(fake_path)], check=True, timeout=60)
            fake_game = subprocess.Popen([str(fake_path)])
            for _ in range(3):
                reused = request({"op":"launch", "mode":"Player", "changes":[
                    {"page":"Appearance", "path":"Title", "value":"Saved without relaunch"}]})
                assert reused["launchResult"] == "existing", reused
                assert reused["playerRunning"] is True
                assert fake_game.poll() is None
            assert json.loads((Path(temp)/"Settings.json").read_text())["BootstrapperTitle"] == "Saved without relaunch"
            request({"op":"launch", "mode":"Invalid", "changes":[]}, ok=False)
            fake_game.terminate()
            fake_game.wait(timeout=15)
            settings = json.loads((Path(temp)/"Settings.json").read_text())
            # External edits must not be overwritten silently.
            path = Path(temp) / "Settings.json"
            settings["BackgroundUpdatesEnabled"] = False
            path.write_text(json.dumps(settings), encoding="utf-8")
            request({"op": "save", "changes": []}, ok=False)
            assert json.loads(path.read_text())["BackgroundUpdatesEnabled"] is False
            process.stdin.close()
            assert process.wait(timeout=30) == 0
            # Removing the font through Mods must not be undone at the next startup.
            custom_font.unlink()
            reopened = subprocess.run([str(engine), "-nativebridge"], input='{"op":"snapshot"}\n',
                                      capture_output=True, text=True, encoding="utf-8", timeout=60)
            assert reopened.returncode == 0, reopened.stderr
            assert not custom_font.exists(), "Removed font was unexpectedly reapplied"

            print("Bridge integration checks passed.")
        finally:
            if fake_game and fake_game.poll() is None:
                fake_game.terminate()
                fake_game.wait(timeout=15)
            if process and process.poll() is None:
                process.terminate()
                process.wait(timeout=15)
            winreg.DeleteKey(winreg.HKEY_CURRENT_USER, KEY)


if __name__ == "__main__":
    if os.name != "nt":
        raise SystemExit("Run on Windows only")
    main()
