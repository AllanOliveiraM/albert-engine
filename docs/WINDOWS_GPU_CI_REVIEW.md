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
