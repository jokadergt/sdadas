# cayb trap — interface C++ e motor Bloxstrap

A interface de configurações foi escrita em C++17/Win32 e organizada nas mesmas seções do Bloxstrap: Integrações, Comportamento, Mods, FastFlags, Aparência, Atalhos e configurações do launcher. Os controles nativos do Windows não têm renderização idêntica aos controles WPF originais.

O motor permanece em C#/.NET 6 e reaproveita as classes de configurações, ViewModels, tarefas de mods e atalhos, instalação, protocolos, download e lançamento originais. C++ envia solicitações JSON por canais privados de entrada/saída para uma instância do motor. As tarefas são executadas pelas classes originais.

## Estado da entrega

- `dist-native/cayb trap.exe`: interface C++ compilada para Windows x64 com LLVM-MinGW/UCRT, ícone de capivara e runtime C++ estático.
- A interface sozinha NÃO instala nem executa Roblox. Precisa de `engine/cayb trap.exe`, compilado a partir do projeto C# modificado.
- O motor modificado NÃO foi compilado ou executado neste ambiente Linux. Portanto, a integração e o funcionamento completo ainda não estão validados.
- O teste de integração para Windows e o workflow estão incluídos, mas não foram executados aqui.
- Integrações personalizadas e FastFlags têm editores JSON nativos.
- Instalação, desinstalação, visualização do bootstrapper, editor de temas, diálogos de arquivo e outros diálogos auxiliares continuam usando a UI original.
- A atualização automática do próprio launcher está desativada para evitar substituição pela versão oficial. A atualização do Roblox continua no motor original.

## Compilar o pacote completo no Windows x64

1. Instale o SDK .NET 6 e MinGW-w64 UCRT64. Adicione `dotnet`, `g++` e `windres` ao PATH. MinGW compila apenas a interface; o motor WPF precisa do SDK Windows.
2. Extraia o ZIP inteiro. O submódulo `wpfui` está incluído.
3. No PowerShell desta pasta, execute:

```powershell
powershell -ExecutionPolicy Bypass -File .\build-cayb-trap.ps1
```

Resultado esperado: `dist/cayb trap.exe` e `dist/engine/cayb trap.exe`. Mantenha a pasta `engine`. A compilação precisa de internet para restaurar NuGet. Os executáveis não têm assinatura digital.

Na primeira abertura, clique em **Instalar / reconectar** para usar o instalador original. A instalação também extrai `cayb trap ui.exe` ao lado do motor instalado. Os atalhos de configurações passam a abrir essa interface C++. Salve as configurações antes de iniciar Roblox ou Studio.

## GitHub Actions

O workflow `.github/workflows/build-cayb-trap.yml` compila as duas partes em Windows, executa um teste isolado do protocolo e publica o pacote como artefato. Ele não foi enviado ou executado em uma conta GitHub nesta entrega.

O teste não inicia Roblox nem confirma compatibilidade com os servidores atuais. Execute-o somente em um runner sem instalação prévia de cayb trap.

## Origem e créditos

Base: https://github.com/bloxstraplabs/bloxstrap

Commit original: `02db76006741f039b79ac7547b8054d7f3ccfc05`. Licença MIT e créditos originais preservados. Personalização independente.

Parser C++: nlohmann/json v3.11.3, com licença em `NativeUI/vendor/LICENSE-json.MIT`. Logo sem texto criada por geração de imagem, convertida para ICO com sete resoluções.
