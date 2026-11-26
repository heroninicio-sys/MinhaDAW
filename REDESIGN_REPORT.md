# Relatório de revisão e alterações

## Corrigido nesta revisão

### Áudio e gravação
- Substituído o caminho de captura que lia o buffer de saída por um `AudioIODeviceCallback` que recebe `inputChannelData` e `outputChannelData` diretamente.
- Adicionado fallback de inicialização de dispositivo: 2 entradas/4 saídas, 2/2, 1/4, 1/2 e por fim 0/2.
- REC é desabilitado sem entrada ativa e a tentativa via atalho também apresenta aviso.
- Writer estéreo recebe sempre dois ponteiros válidos; mono é duplicado para o segundo canal.
- Monitoramento mono também alimenta os dois canais.
- Controles de transporte mantêm o estado PLAY/REC sincronizado.
- Flags e valores de mixagem acessados pelo callback usam atomics.
- Buffer de gravação e histórico de picos são pré-alocados/limitados no caminho normal.

### Clips e MIDI
- Clips de áudio são preparados para resampling na taxa do dispositivo.
- Arquivos mono são centralizados em L/R.
- Buffers de faixa usam o número exato de amostras do callback, evitando avançar o leitor por amostras que não foram enviadas à saída.
- Eventos MIDI validam canal, nota, velocidade e comprimento.
- Leituras/edições de clips na Timeline e Piano Roll usam o lock do motor.

### Sessões e plugins
- Load valida o array de faixas antes de tocar no projeto atual e monta um modelo temporário antes do commit.
- Sessões passam a guardar Master Gain, efeitos nativos do Master e estados/caminhos de VST3 do Master.
- VST3 pode ser selecionado como arquivo ou diretório bundle.
- Callbacks assíncronos de seleção de arquivo/menus verificam `SafePointer` e a existência da faixa antes de acessar referências.
- Bypass VST3 usa o parâmetro de bypass do plugin quando existe e bypass de host por pass-through quando não existe; estado é persistido na sessão.
- Exclusão de módulos no Rack é adiada até o callback do botão terminar.
- Preferências de áudio e janela de exportação são controladas pela janela principal para não manter referências após o encerramento.

### Exportação
- Resampling offline calcula a quantidade de amostras de saída e mantém o resto fracionário entre blocos.
- Entrada de resampler tem padding para leitura de interpolação.
- Exportação mono combina L/R.
- Inserts VST3/nativos e ganho do Master entram no render.
- Falha do FFmpeg mantém uma cópia WAV em vez de descartar a mixagem.

### Interface
- LookAndFeel nativo redesenhado com superfícies grafite/azul, bordas frias, ciano e controles mais consistentes.
- Barra superior, Timeline, cabeçalhos de faixa, Mixer, Rack, Piano Roll e overlays alinhados à mesma paleta.
- Altura das faixas e dock do Mixer ampliadas; tamanho mínimo da janela evita que a barra de transporte seja comprimida.

## Validação executada

- Verificação estática de delimitadores `{}`, `[]`, `()` nos headers/sources: passou.
- Revisão dos caminhos de callback, gravação, load/save e exportação: concluída no código.
- Configuração CMake tentou baixar JUCE 8.0.4, mas falhou exclusivamente porque o ambiente não resolveu `github.com` (`Could not resolve host`). Portanto, não há confirmação de compilação/execução nesta sessão.
- Workflow Windows atualizado para compilar Release em CI; o resultado precisa ser confirmado no GitHub Actions.

## Pontos que exigem teste real

- Compatibilidade de drivers ASIO/WASAPI e contagem de canais de cada interface.
- VST3 de terceiros (incluindo bundles instalados em diretórios).
- Latência/estabilidade de gravação e playback em blocos variáveis.
- Qualidade do resampling e sincronismo em sessões longas.
- Exportação em todas as taxas/formats e FFmpeg instalado.
