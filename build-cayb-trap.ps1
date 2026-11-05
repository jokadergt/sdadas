$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot
if (-not (Get-Command dotnet -ErrorAction SilentlyContinue)) {
    throw 'Instale o SDK .NET 6 para Windows antes de compilar.'
}
if (-not (Test-Path 'wpfui/src/Wpf.Ui/Wpf.Ui.csproj')) {
    throw 'O submodulo wpfui esta ausente. Use git submodule update --init --recursive.'
}
dotnet publish Bloxstrap/Bloxstrap.csproj -c Release -r win-x64 --self-contained true -p:PublishSingleFile=true -p:IncludeNativeLibrariesForSelfExtract=true -o dist
if ($LASTEXITCODE -ne 0) { throw 'Falha na compilacao.' }
if (-not (Test-Path 'dist/cayb trap.exe')) { throw 'Executavel esperado nao encontrado.' }
Write-Host "Pronto: $PSScriptRoot/dist/cayb trap.exe"
