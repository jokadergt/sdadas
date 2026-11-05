# cayb trap

Personalizacao do Bloxstrap original, mantendo suas telas WPF e componentes.
Base: https://github.com/bloxstraplabs/bloxstrap
Commit: 02db76006741f039b79ac7547b8054d7f3ccfc05

Alteracoes: nome do aplicativo/executavel, icone de capivara e atualizacao automatica do proprio launcher desativada para evitar substituicao por Bloxstrap. A instalacao/atualizacao do Roblox permanece no codigo original. Creditos e licenca MIT originais preservados.

Este pacote contem codigo-fonte, NAO um executavel compilado. Nao foi possivel compilar nem testar o aplicativo Windows no ambiente de preparacao.

## Gerar o executavel no Windows x64

1. Instale o SDK .NET 6 para Windows.
2. Extraia todo este ZIP.
3. Abra o PowerShell nesta pasta e execute:

```powershell
powershell -ExecutionPolicy Bypass -File .\build-cayb-trap.ps1
```

Resultado esperado: `dist/cayb trap.exe`, com runtime incluido e sem assinatura digital.
A compilacao precisa de internet para restaurar pacotes NuGet.
O submodulo wpfui ja esta incluido neste pacote.
