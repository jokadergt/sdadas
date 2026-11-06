# cayb trap — interface C++ e motor Bloxstrap

A interface de configurações foi escrita em C++17/Win32 e organizada nas mesmas seções do Bloxstrap: Integrações, Comportamento, Mods, FastFlags, Aparência, Atalhos e configurações do launcher. Os controles nativos do Windows não têm renderização idêntica aos controles WPF originais.

O motor permanece em C#/.NET 6 e reaproveita as classes de configurações, ViewModels, tarefas de mods e atalhos, instalação, protocolos, download e lançamento originais. C++ envia solicitações JSON por canais privados de entrada/saída para uma instância do motor. As tarefas são executadas pelas classes originais.

## Estado da entrega

- `dist-native/cayb trap.exe`: interface C++ compilada para Windows x64 com LLVM-MinGW/UCRT, ícone de capivara e runtime C++ estático.
- A interface sozinha NÃO instala nem executa Roblox. Precisa de `engine/cayb trap.exe`, compilado a partir do projeto C# modificado.
- A interface e o motor foram compilados em Windows pelo GitHub Actions. O teste isolado de integração passou. O funcionamento em uma sessão real do Roblox não foi testado.
- O workflow recompila as duas partes e executa os testes de integração em Windows.
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

O workflow `.github/workflows/build-cayb-trap.yml` compila as duas partes em Windows, executa um teste isolado do protocolo e publica o pacote como artefato. Ele está configurado no repositório jokadergt/sdadas.

O teste não inicia Roblox nem confirma compatibilidade com os servidores atuais. Execute-o somente em um runner sem instalação prévia de cayb trap.

## Origem e créditos

Base: https://github.com/bloxstraplabs/bloxstrap

Commit original: `02db76006741f039b79ac7547b8054d7f3ccfc05`. Licença MIT e créditos originais preservados. Personalização independente.

Parser C++: nlohmann/json v3.11.3, com licença em `NativeUI/vendor/LICENSE-json.MIT`. Logo sem texto criada por geração de imagem, convertida para ICO com sete resoluções.

## Fonte Minecraft dentro do Roblox

A fonte fornecida foi embutida no motor e é aplicada uma vez como
`Modifications/content/fonts/CustomFont.ttf`, pelo mecanismo de fonte personalizada
do Bloxstrap. Isso acontece na instalação ou na primeira abertura desta versão.
Feche o Roblox e inicie-o pelo cayb trap para o bootstrapper aplicar o mod.
A interface C++ usa Segoe UI, como antes.

Para trocar ou remover a fonte depois, use a seção Mods. A remoção fica respeitada
nas próximas aberturas. Jogos que usam fontes próprias carregadas como assets
podem não refletir a substituição das famílias locais do cliente.

## Preset automático de FastFlags

Na primeira abertura desta versão, o motor grava um preset com as 18 chaves da lista pública do Roblox consultada em 05/11/2026. A configuração anterior fica em `Backups/ClientAppSettings.before-allowed-preset.json`. Os valores são um preset equilibrado próprio, não valores oficiais padrão: D3D11 como única preferência de renderizador, texturas nível 2, MSAA 2x, distâncias de grama de 50/100 e opções de depuração visuais desligadas. As outras chaves também ficam explícitas no JSON. Não há promessa de aumento de FPS nem de efeito visível para toda flag.

A opção **Preset automático** mantém o JSON limitado às 18 chaves e preenche as ausentes ao salvar. Desligue-a para edição manual, por exemplo para testes no Studio. O arquivo é aplicado na próxima inicialização do Roblox pelo cayb trap, com o jogo fechado. A lista efetiva de cada cliente/canal pode mudar; esta é a lista pública consultada, não uma garantia futura.

Fonte: https://devforum.roblox.com/t/allowlist-for-local-client-configuration-via-fast-flags/3966569

## Interface e som

Botões, seletores, caixas de texto, navegação e cartões usam cantos arredondados. Os cliques têm um som curto de bolha gerado para o aplicativo, embutido no executável. A opção **Som de bolha nos cliques**, na seção cayb trap, permite desligá-lo. Os diálogos auxiliares WPF ainda são os originais.

## Limpeza opcional de cache em segundo plano

Na seção **Comportamento**, habilite **Limpar cache de arquivos em segundo plano** e salve. Ao iniciar Roblox/Studio, um processo auxiliar solicitará administrador pelo UAC. Se a autorização for negada, a limpeza não roda. Quando houver jogo aberto e uso de RAM de pelo menos 85%, o auxiliar libera o cache de arquivos do Windows, no máximo uma vez a cada cinco minutos; a primeira verificação que pode limpar ocorre após cinco minutos. Ele encerra após o jogo fechar ou a opção ser desativada, com até um minuto para perceber o fechamento e até 15 segundos para perceber a desativação.

O recurso usa `SetSystemFileCacheSize(-1,-1,0)`: não limpa a lista standby inteira, não encerra programas, não reduz à força o working set do Roblox e não muda os limites permanentes do cache. O Windows pode preencher o cache novamente; limpar pode causar novas leituras do disco e não garante melhor desempenho. O teste de CI verifica a persistência da opção, mas não executa a limpeza privilegiada.

## Reutilizar a instância aberta

Os botões Roblox/Studio salvam as alterações automaticamente. Se já houver uma instância do produto na mesma sessão do Windows, o launcher tenta trazê-la para frente e não inicia outra. Há uma nova checagem imediatamente antes de iniciar o executável, para cobrir a espera por downloads/atualizações. Instâncias de outros usuários/sessões não são selecionadas.

A limpeza de cache habilitada pode começar a acompanhar o jogo já aberto, mantendo a autorização de administrador, o limite de 85% e o intervalo mínimo já descritos. Fonte, mods de arquivos e FastFlags continuam preparados nos arquivos para a próxima inicialização: este mecanismo não injeta código ou valores na memória do Roblox e não recarrega essas configurações na sessão atual. O launcher mantém a sessão existente aberta e não força reinicialização.

O teste Windows usa um processo de teste próprio com o nome do executável do Player para verificar detecção, reutilização e salvamento automático, sem iniciar ou baixar Roblox real. Isso não testa uma sessão online do Roblox.

## Cursores do Sweezy

A seção **Cursores** permite pesquisar no Sweezy Cursors, selecionar uma prévia e clicar em **Usar no Roblox**. A integração usa uma API interna por JSON no canal existente entre interface e motor; não instala servidor HTTP, navegador embutido nem processo permanente adicional.

A API consulta a busca pública `/search/?s=...`, mantém até oito pesquisas em cache por cinco minutos e retorna até 24 resultados por pesquisa. Refine o termo se precisar de outros resultados. Somente a prévia selecionada baixa as duas imagens PNG públicas usadas pelo próprio site. O aplicativo as converte para um canvas transparente de 64 × 64, com desenho de até 24 pixels e o ponto de clique alinhado ao centro, usando os deslocamentos publicados pelo Sweezy. Essa conversão local evita depender de links de download temporários do armazenamento do site. Cada imagem tem limite de 2 MB e 512 × 512 pixels; até 16 pares ficam no cache temporário por até 24 horas. O cliente não executa scripts, anúncios, extensões, arquivos EXE, CUR ou ANI do site.

**Usar no Roblox** prepara `ArrowCursor.png` e `ArrowFarCursor.png` na pasta de modificações do Bloxstrap. Feche o jogo e abra-o pelo cayb trap para aplicar. Se já houver uma sessão aberta, ela permanece aberta e o cursor fica preparado para o próximo início. Experiências que definem o próprio cursor podem substituí-lo. Cursores animados do site usam as imagens PNG estáticas publicadas nas prévias; não há promessa de animação no jogo.

**Restaurar padrão** remove essas substituições para que o mecanismo original restaure os arquivos padrão na próxima abertura. Substituições anteriores ficam em `Backups/Cursors`. As criações, nomes e direitos pertencem aos autores/Sweezy; o botão **Ver no Sweezy** abre a página original. A integração não é afiliada ao site e não contorna restrições de acesso: se houver bloqueio, limite de requisições ou mudança no HTML, o aplicativo mostra uma mensagem em português.

A busca pública e os downloads usados foram verificados em 06/11/2026. O build testa análise do HTML, validação PNG, instalação/restauração em pasta temporária e tenta uma pesquisa e download reais. Indisponibilidade de rede do serviço é reportada separadamente, sem fingir resultados. O funcionamento visual dentro de uma sessão real do Roblox não é testado no CI.
