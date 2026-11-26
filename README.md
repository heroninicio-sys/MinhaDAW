# MinhaDAW Studio

DAW desktop em C++/JUCE. Esta revisão atualiza a interface nativa e corrige pontos de segurança, áudio, gravação, sessão e renderização identificados na análise do código enviado.

## Requisitos

- Windows 10/11 x64 (alvo atual do workflow)
- CMake 3.22 ou superior
- Visual Studio 2022 com workload **Desktop development with C++**
- Conexão com a internet na primeira configuração, pois o CMake baixa JUCE 8.0.4 via FetchContent
- FFmpeg no PATH ou `ffmpeg.exe` ao lado do executável para exportação MP3/OGG/WMA

## Compilar no Windows

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release --parallel
```

O executável é gerado em:

```text
build/MinhaDAW_artefacts/Release/MinhaDAW Studio.exe
```

O workflow `.github/workflows/build-windows.yml` executa a mesma configuração em cada push/PR para `main` e publica o executável como artefato.

## Principais mudanças

- Novo tema nativo grafite/azul-marinho, ciano e acentos por faixa; barra superior e Mixer com hierarquia visual renovada.
- Callback direto de `AudioIODeviceCallback`, permitindo receber os ponteiros reais de entrada do dispositivo. O caminho anterior baseado em `AudioAppComponent` não expunha de forma confiável as entradas ao código de gravação.
- Fallback de inicialização para dispositivos com 2 ou 1 entradas, 4 ou 2 saídas e, por fim, reprodução sem entrada; o botão REC fica indisponível quando não há entrada ativa.
- Gravação estéreo correta: entrada mono é duplicada para L/R em vez de enviar um array de um canal a um writer estéreo.
- Transporte PLAY/REC alternável e estado sincronizado com STOP/atalhos.
- Controles de ganho/pan/mute/solo/arm/monitor e parâmetros de efeitos nativos protegidos com atomics; bypass de efeitos nativos também é atômico.
- Buffer de áudio com tamanho exato do callback e waveform de gravação limitada para evitar crescimento ilimitado no callback de áudio.
- Resampling de clips para a taxa de amostragem do dispositivo, incluindo duplicação de arquivos mono para o centro estéreo.
- Exportação com contagem de amostras resampladas corrigida, mixagem mono real, processamento de inserts do Master e ganho Master; se FFmpeg falhar, a mixagem WAV é preservada.
- Salvamento de ganho e cadeia de efeitos/plugins do Master junto com as faixas, incluindo o estado de bypass dos VST3.
- Carregamento de sessão transacional: um arquivo sem estrutura válida não apaga a sessão aberta.
- Tratamento de VST3 em diretórios e callbacks assíncronos com `SafePointer` nos fluxos de arquivo/menus.
- Janelas de preferências de áudio e renderização agora pertencem à janela principal e são fechadas antes da destruição dos objetos que referenciam.

## Testes manuais recomendados

1. Abrir Preferences e selecionar uma interface com entradas e saídas.
2. Gravar sinal mono e estéreo; confirmar waveform e reprodução dos takes.
3. Testar PLAY, STOP, REC, espaço, R, rewind e seek.
4. Importar WAV mono e estéreo com taxas de amostragem diferentes da interface.
5. Testar mute, solo, arm, monitor, pan e faders.
6. Adicionar/remover efeitos nativos e VST3 em faixas e no Master.
7. Salvar, fechar e reabrir uma sessão com clips MIDI/áudio, plugins e Master FX.
8. Exportar WAV estéreo/mono e, com FFmpeg instalado, MP3/OGG/WMA em taxas diferentes.
9. Testar dispositivos com duas e quatro saídas e sem entradas.

## Limitações conhecidas / validação pendente

- A compilação CMake/JUCE não pôde ser concluída neste ambiente: o runner não conseguiu resolver `github.com` ao baixar JUCE 8.0.4. O workflow do GitHub Actions foi atualizado para executar a compilação em um ambiente com rede.
- O scanner VST3 ainda executa a descoberta no processo principal; plugins de terceiros problemáticos podem atrasar a varredura. O arquivo `DeadMansPedal` ajuda a pular caminhos que causaram falha anterior, mas não equivale a isolamento por processo.
- O render offline ainda é síncrono na janela de exportação e pode deixar a interface ocupada em sessões longas.
- O motor ainda usa um `CriticalSection` compartilhado entre UI e callback de áudio. As alterações reduzem corridas de dados, mas uma arquitetura lock-free/command-queue é uma evolução futura para uso profissional de baixa latência.

## Commit sugerido

`feat: redesign JUCE studio UI and harden audio/session workflows`
