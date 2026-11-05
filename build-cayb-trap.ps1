$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot
foreach ($tool in @('dotnet', 'g++', 'windres')) {
    if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) {
        throw "Ferramenta ausente: $tool. Instale SDK .NET 6 e MinGW-w64 UCRT64 e adicione-os ao PATH."
    }
}
if (-not (Test-Path 'wpfui/src/Wpf.Ui/Wpf.Ui.csproj')) { throw 'Submodulo wpfui ausente.' }
New-Item -ItemType Directory -Force dist-native | Out-Null
Push-Location NativeUI
try {
    windres app.rc -O coff -o app.res.o
    if ($LASTEXITCODE -ne 0) { throw 'Falha nos recursos da interface.' }
    g++ main.cpp app.res.o -std=c++17 -O2 -Wall -Wextra -Wno-missing-field-initializers -municode -mwindows -static -lcomctl32 -lshell32 -ldwmapi -luxtheme -o '../dist-native/cayb trap.exe'
    if ($LASTEXITCODE -ne 0) { throw 'Falha na compilacao C++.' }
} finally { Pop-Location }
dotnet publish Bloxstrap/Bloxstrap.csproj -c Release -r win-x64 --self-contained true -p:PublishSingleFile=true -p:IncludeNativeLibrariesForSelfExtract=true -o dist/engine
if ($LASTEXITCODE -ne 0) { throw 'Falha na compilacao do motor C#.' }
Copy-Item 'dist-native/cayb trap.exe' 'dist/cayb trap.exe' -Force
Copy-Item LEIA-ME-CAYB-TRAP.md dist/LEIA-ME.md -Force
Write-Host 'Pronto: dist/cayb trap.exe. Mantenha a pasta engine ao lado do executavel.'
