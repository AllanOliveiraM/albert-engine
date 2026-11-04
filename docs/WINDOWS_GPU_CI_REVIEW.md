# Falhas gráficas no CI Windows — 3/11/2026

Execuções relatadas pelo usuário, ambas no commit `7b4768a`:

- [Build and tests](https://github.com/AllanOliveiraM/albert-engine/actions/runs/41736117339):
  Linux aprovado; Windows compilou e falhou nos testes `visual_sun` e
  `visual_solar_flare`. O segundo registra perda do dispositivo Direct3D
  (`0x887A0005`), seguida de comandos submetidos ao dispositivo perdido.
- [Portable releases](https://github.com/AllanOliveiraM/albert-engine/actions/runs/41736473452):
  Linux aprovado; etapa de build/testes Windows falhou. A publicação foi
  bloqueada corretamente. Não há diagnóstico de compilador na anotação pública.

As anotações públicas foram conferidas pela API do GitHub. Depois, o usuário
forneceu os arquivos `logs_113099456122.zip` e `logs_113100378833.zip` na raiz do
repositório. Foram lidos e excluídos a pedido do usuário; não fazem parte do
commit. Os logs confirmam que a compilação
terminou e a mesma sequência se repetiu nas duas execuções:

- `visual_sun`: acesso inválido à memória (`SEGFAULT`), em 134,49 e 134,60 s.
- `visual_solar_flare`: abort `0xc0000409`, em 9,29 e 9,66 s, com dispositivo
  Direct3D perdido. Pode ser consequência do problema no teste anterior.
- `visual_webgpu` e, mais tarde, `webgpu_atmosphere` passaram. Portanto, não se
  trata de ausência geral de suporte WebGPU no runner.

A saída do teste solar contém somente o aviso de sondagem OpenGL. Esse aviso
também pode ocorrer antes de escolher Direct3D e não comprova a causa da falha.
O aviso sobre Node.js das actions é independente desses erros. Sem stdout
descarregado imediatamente ou dump, o ponto exato do acesso inválido não ficou
registrado; a falha de mapeamento ignorada é uma possibilidade, não prova de
que foi essa a origem do `SEGFAULT` observado.

## Correção proposta e diagnóstico

`Atmosphere::precompute` usava oito etapas por submissão em adaptadores CPU e só
esperava a GPU após enfileirar toda a tabela. Submissões consecutivas não são
barreiras de conclusão; o driver pode acumular trabalho. Em adaptadores CPU,
agora se submete uma fatia e se espera sua conclusão antes da próxima. Todos os
texels, amostras e ordens de espalhamento são preservados. A execução assíncrona
de GPUs físicas continua igual.

Isso limita um risco de trabalho prolongado no WARP. A relação com TDR é uma
**hipótese**, não uma reprodução local: a
[documentação da Microsoft](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/timeout-detection-and-recovery)
descreve o reset do dispositivo quando uma operação excede o prazo de execução
ou preempção. Nenhum limite do sistema foi aumentado/desativado.

O readback dos testes agora confere o status do mapeamento antes de acessar a
memória. A saída é descarregada imediatamente e identifica a inicialização,
pré-cálculo e cena em execução, para preservar a última etapa em caso de abort.

Os dois workflows Windows passam a guardar o resultado JUnit e os logs do CTest
nas falhas. As anotações passam a incluir a classificação e duração do teste,
mesmo sem stdout, com cobertura para timeout/crash, CRLF, saída longa e arquivos
JUnit ausentes/incompletos. Nenhum dos testes gráficos foi removido, desativado
ou teve suas exigências visuais reduzidas.

## Validação

Compilação de `AlbertEngine`, `visual_test`, `atmosphere_test` e
`coastal_optics_test` aprovada com um job. Cinco testes direcionados aprovados em
164,26 s: Sol, reflexos, relatório de falhas, atmosfera e coluna marinha. O teste
do relatório contém quatro casos. XMLs dos workflows usam caminhos absolutos,
pois `ctest --test-dir` muda o diretório de trabalho antes de escrever o JUnit.
Ambos os YAMLs foram lidos com sucesso; o PowerShell não foi executado aqui.

Evidências em `build/mission-review/windows-ci/`. Menor RAM disponível na rodada
de testes: 6.979,4 MiB; pico RSS amostrado da validação: 310,9 MiB. Nenhuma
proteção de memória foi acionada. Não há Windows/WARP neste ambiente; somente
uma nova execução no runner Windows poderá confirmar a correção das falhas
relatadas. Até lá, não considerar o CI Windows resolvido.

## Nova execução: pré-cálculo concluído, falhas persistem

[Execução de `984e922`](https://github.com/AllanOliveiraM/albert-engine/actions/runs/41756202492):
Linux aprovado; Windows compilou e voltou a falhar nos mesmos dois testes.
As novas anotações/JUnit localizaram melhor as falhas:

- `visual_sun`: `SEGFAULT`, 134,535 s. O log já registra **atmosphere ready**
  antes de **sun-day**. Portanto, o pré-cálculo não é o ponto do crash observado;
  aguardar fatias menores não resolveu essa falha.
- `visual_solar_flare`: `0xc0000409`, 9,497 s. Todos os casos sem MSAA passaram,
  inclusive bloqueio total/parcial, fisheye e bloqueador distante. `flare-off-4`
  também terminou; a perda do dispositivo ocorre em `flare-on-4`.

A hipótese inicial de consequência do teste anterior não basta para explicar
essa diferença. Ainda não há dump que identifique a instrução/driver responsável.
O backend executado foi o Microsoft Basic Render Driver (D3D12/WARP); o aviso
da sondagem OpenGL não impediu os casos aprovados.

### Simplificação dos shaders, preservando o modelo

- O shader de fundo escolhe primeiro o percurso (céu ou horizonte) e chama
  `skySegment` uma única vez, evitando duplicar todo o cálculo atmosférico
  expandido pelo compilador HLSL.
- A quadratura marinha continua esférica, com seis pontos por metade e a mesma
  extensão. Interseções vazias viram intervalos de comprimento zero por
  aritmética, sem retornos condicionais antes das leituras das LUTs.
- Os 32 pontos de visibilidade solar passam a ser calculados em 32 invocações
  de um grupo, em vez de um laço serial com outro laço MSAA dentro. As quatro
  leituras de profundidade têm índices constantes. Uma barreira seguida de
  redução sem texturas conserva a ordem original da soma. Amostras rejeitadas
  também alcançam a barreira. Nenhuma amostra, efeito ou teste foi desativado.
- Acrescentado caso de Sol cortado pela borda da tela, com amostras dentro e
  fora da imagem, além das verificações existentes de oclusão parcial e nuvens.
- `ALBERT_GPU_TRACE=1` registra início/fim da criação de módulos e pipelines.
  Habilitado nos testes Windows dos dois workflows; os logs solares distinguem
  codificação/submissão e readback do céu/disco.

Essas mudanças reduzem a complexidade do caminho em investigação. A origem
exata das falhas Windows continua por confirmar; aprovação Linux e equivalência
das capturas não substituem uma nova execução no WARP.

### Validação local da simplificação

Compilação de `AlbertEngine`, `visual_test`, `atmosphere_test` e
`coastal_optics_test` aprovada, com um job e proteção de RAM. A rodada em
llvmpipe passou os oito cenários solares, reflexos em MSAA 1×/4× (incluindo
oclusão, borda da tela, atmosfera e nuvens), `visual_webgpu`,
`ci_failure_reporting`, `webgpu_atmosphere` e `coastal_optics`, em 196,92 s.
RAM disponível mínima: 7.373,6 MiB; pico RSS amostrado da validação: 1.434,3 MiB.

Comparadas as 24 capturas comuns antes/depois: 23 idênticas byte a byte; a
imagem do limbo orbital mudou até dois níveis por canal (de 255), em 5.878 dos
442.368 bytes de pixels, compatível com arredondamentos da avaliação reorganizada.
Nenhuma exigência visual foi relaxada. Evidências em
`build/mission-review/windows-ci/{before-refactor,after-refactor}.log`,
`capture-comparison.txt` e diretórios de capturas nesse mesmo local.

Os cenários solares, reflexos e teste numérico da névoa também passaram na
NVIDIA MX350 e Intel Iris Xe (6,07 s no total; log `refactor-native.log`). RAM
disponível mínima nessa rodada: 7.985,3 MiB. As três rodadas não registraram
erros WebGPU. Os dois YAMLs foram lidos com sucesso; `git diff --check` passou.

## Execução de `ee42081`: falha localizada na criação do pipeline

[Nova execução](https://github.com/AllanOliveiraM/albert-engine/actions/runs/41761113285):
Linux aprovado; Windows compilou. O relatório público agora contém somente
`visual_sun` como falha (`SEGFAULT`, 134,142 s). `visual_solar_flare` deixou de
ser relatado como falha; o XML completo não está acessível sem autenticação
para distinguir aprovação de eventual skip nesta rodada.

O rastreamento termina exatamente em:

```text
Solar: atmosphere ready
Solar render: sun-day
Solar: sky encode/submit
WebGPU module begin: albert:builtin
WebGPU module ready: albert:builtin
WebGPU module begin: albert:atmosphere
WebGPU module ready: albert:atmosphere
WebGPU pipeline begin: albert:atmosphere (1x MSAA)
```

Portanto, a falha ocorre dentro de `wgpuDeviceCreateRenderPipeline` para o céu,
antes da submissão do frame/readback; não é uma evidência de falta de VRAM,
falha de mapeamento ou pré-cálculo interrompido. Sem dump ainda não se identifica
a instrução nativa responsável.

### Próxima correção: compilador D3D12 explícito e distribuído

O contexto Windows passa a pedir DXC por `WGPUInstanceExtras`, com caminho
absoluto UTF-8 ao lado do executável, independente de cwd/PATH. A versão
[v1.8.2505.1 oficial da Microsoft](https://github.com/microsoft/DirectXShaderCompiler/releases/tag/v1.8.2505.1)
é fixada por SHA-256 (`9ad895a6…215ea8a4`); o hash foi também calculado no ZIP
baixado e confere com o digest publicado. A API da wgpu-native v29 documenta
DXC mínimo v1.8.2502 e Windows/WDDM compatível. Nenhuma expressão do modelo
atmosférico foi removida nesta etapa.

`dxcompiler.dll` e `dxil.dll` x64 acompanham cada executável GPU e a distribuição
instalada, com os três arquivos de licença do pacote. Ausência de qualquer DLL
é diagnosticada no início. O uso de DXC evita depender do compilador legado do
host; sua relação com este crash precisa ser confirmada no runner.

O CI Windows executa `visual_sun` primeiro, antes dos testes longos. O restante
da suíte exclui somente esse caso já executado; a cobertura total é preservada,
sem testar o mesmo caso duas vezes. `ALBERT_REQUIRE_GPU=1` faz a inicialização
dos casos solares/reflexos falhar em vez de retornar skip nos workflows de build
e release. Isso também evita aceitar um pacote sem as DLLs como CI aprovado.

Verificados os blocos reais de cópia/instalação CMake em um projeto pequeno,
com caminhos contendo espaços: os dois binários e as três licenças coincidem
byte a byte com o pacote oficial. Esse ensaio verifica empacotamento, **não**
execução Windows; não executou binários Microsoft no Linux. Evidências em
`build/mission-review/windows-ci/dxc package check/`.

Build Linux dos quatro alvos aprovada (um job, proteção de RAM). A rodada
direcionada com GPU obrigatória passou Sol/reflexos na MX350 e Iris Xe e o
teste numérico da névoa nos três adaptadores (incluindo llvmpipe), em 5,06 s;
RAM disponível mínima de 8.122 MiB. Logs `dxc-final-build.log` e
`dxc-linux-regressions.log`. Os imports PE das novas DLLs foram inspecionados:
componentes do Windows/UCRT, sem dependência nova de VCRUNTIME/MSVCP. Os YAMLs
foram lidos com sucesso e `git diff --check` passou. Não há compilador/runtime
Windows local: o bloco nativo e o uso efetivo de DXC ainda dependem do novo CI.
