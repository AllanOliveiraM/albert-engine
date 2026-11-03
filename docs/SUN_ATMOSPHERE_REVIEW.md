# Disco solar e composição atmosférica — 3/11/2026

Segunda prioridade da [fila de ajustes](visual-improvements-queue.md), conforme pedido do usuário. Esta etapa corrige o Sol escuro/preto; não encerra todo o refinamento visual da atmosfera.

## Defeito reproduzido

O shader solar desenhava uma textura na faixa de brilho de uma superfície comum e multiplicava somente pela transmissão atmosférica. Como o disco é opaco, substituía também o céu já desenhado atrás dele. A luz espalhada pelo ar **à frente** do Sol desaparecia nessa área. A saída ainda ignorava a exposição fotográfica usada pelo céu.

O teste usa os shaders de produção e as tabelas atmosféricas do Albert, com uma textura branca controlada para isolar a composição. Portanto, as imagens são diagnósticos renderizados, não fotografias de referência ou imagens finais para o README.

| Cenário / pixel central | Antes: RGB | Depois: RGB |
| --- | --- | --- |
| Dia, 30° de elevação | 226, 203, 171, mais escuro que o céu | 255, 255, 255 |
| Horizonte, 0,1°, névoa adicional de 0,04/km | 0, 0, 0 | 255, 106, 49 |
| Névoa densa, 0,4°, 0,3/km | 0, 0, 0 | 4, 2, 4, conservando o céu à frente |
| Sol atrás da Terra, −3° | 0, 0, 0 | 0, 0, 0 |
| Limbo em órbita, altitude de 400 km | 157, 68, 24 | 255, 255, 255 |
| Espaço | 255, 250, 241 | 255, 255, 255 |
| Inspeção solar, exposição de 0,00003 | 255, 250, 241, sem resposta à exposição | 234, 233, 231 |

Valores acima obtidos em software. Os casos incluem a comparação com a mesma cena sem o disco: ele não pode substituir o céu por um buraco escuro. Também há verificações de exposição, ocultação pela Terra e ausência de erros WebGPU.

## Correção e fundamento

- A emissão do disco passa a ser radiância, usando a escala de irradiância já adotada na cena (2,4 a 1 UA), dividida pelo ângulo sólido aparente. O valor é uma escala interna do renderizador, não 2,4 W/m². A radiância emitida permanece constante com a distância.
- A relação segue `GetSolarRadiance` da [implementação de referência de Eric Bruneton](https://ebruneton.github.io/precomputed_atmospheric_scattering/atmosphere/model.cc.html): irradiância dividida por π vezes o quadrado do raio angular. O raio angular de 0,00465047 rad vem dos valores já usados no Albert, 695.700 km e 1 UA.
- `airApply` aplica transmissão **e** radiância espalhada antes do mesmo mapeamento de exposição/ACES utilizado pelo céu. Isso segue a composição das [funções atmosféricas de referência](https://ebruneton.github.io/precomputed_atmospheric_scattering/atmosphere/functions.glsl.html).
- A interseção analítica com o corpo oculta a emissão abaixo do horizonte, inclusive onde a tesselação grossa do globo deixa um pequeno vão.
- Ao selecionar o Sol como foco, a exposição se adapta a 0,00003 para permitir inspecionar a fotosfera; ao olhar para ele numa cena diurna, sua emissão continua intensa. O escurecimento aproximado do limbo e a textura incluída foram preservados.

Sem novas texturas, passes ou malhas nessa correção. Não muda a iluminação física dos objetos ou a missão. A atmosfera continua com os limites e aproximações do modelo RGB existente; o coeficiente e a estrutura espacial da névoa não foram recalibrados nesta etapa.

## Evidências e reprodução

```sh
cmake --build build/linux --target AlbertEngine visual_test menu_layout_test -j2
ALBERT_GPU=software ./build/linux/tests/visual_test sun-check build/mission-review/sun-after
ALBERT_GPU=high ./build/linux/tests/visual_test sun-check build/mission-review/sun-after-high
ALBERT_GPU=low ./build/linux/tests/visual_test sun-check build/mission-review/sun-after-low
```

Sete capturas por execução. A regressão reproduziu seis verificações falhando antes da alteração, e passou depois. Comparação inspecionada: `build/mission-review/sun-after/comparison.png`; originais em `sun-before/` e `sun-after/`. Logs em `build/mission-review/gpu-texture-lifetime/sun-*.log`.

Os sete cenários também passaram nas execuções com `ALBERT_GPU=high` e `ALBERT_GPU=low`, com diferenças pequenas de arredondamento nos pixels de névoa/horizonte.

CTest direcionado: `menu_layout`, `visual_webgpu`, `visual_sun` e `webgpu_atmosphere` aprovados (97,89 s). O teste de layout cobre nove tamanhos/escalas com os novos campos UTC. Compilação de `AlbertEngine` aprovada.

## Continuação: reflexos de lente discretos

Acrescentado `SolarFlare`, uma aproximação fotográfica leve de halo e três reflexos internos suaves. A causa óptica e o movimento em relação ao eixo da imagem seguem o fenômeno descrito por [Hullin et al., SIGGRAPH 2011](https://resources.mpi-inf.mpg.de/lensflareRendering/). Esta implementação **não** traça raios por uma prescrição óptica medida, nem pretende reproduzir uma lente específica. Intensidades/formas dos reflexos são escolhas visuais, limitadas para preservar a cena.

- Somente a malha solar visível aciona o efeito; uma luz direcional sozinha não o produz. Câmera ortográfica, Sol oculto ou fora da vista não deixam reflexos de frames anteriores.
- Uma pequena etapa compute mede 32 pontos do disco contra a profundidade opaca, respeitando MSAA 1x/4x. A medição é feita antes de as nuvens escreverem profundidade; depois usa a transmissão contínua do volume para evitar o salto no limiar de opacidade de 50%.
- Atmosfera, névoa, horizonte curvo e cobertura de nuvens atenuam a energia óptica antes da composição. A energia acompanha exposição e tamanho angular; não é extraída da imagem solar já saturada.
- Reflexos são compostos antes do redimensionamento/fisheye e da interface. Não usam readback, histórico temporal ou alvos novos do tamanho da tela. Recursos próprios: buffers de 48 e 16 bytes, pipelines e vínculos. A etapa preserva o depth/MSAA entre passes, cujo tráfego adicional precisa ser considerado no desempenho total.
- Ligado por padrão, com controle persistente **Ajustes → Efeitos → Reflexos de lente** (`solar_flare` nas preferências). Intensidade padrão limitada a 0,035; não altera a luz que ilumina objetos.

### Testes de reflexos

`visual_test flare-check DIRETORIO` verifica brilho limitado, bloqueio total/parcial por objeto opaco, Sol oculto/atrás da câmera, repetição determinística, MSAA 1x/4x, resolução reduzida, fisheye, atmosfera e nuvem volumétrica. Um volume opaco deve eliminar exatamente a contribuição; volumes a 45% e 55% devem produzir atenuação gradual, sem salto ao escrever profundidade.

Os casos passaram em llvmpipe, MX350 e Iris Xe no executável isolado de revisão, enquanto a build integrada era reconstruída. Logs `flare-software.log`, `flare-high.log` e `flare-low.log` em `build/mission-review/gpu-texture-lifetime/`. Depois, a build integrada passou e os sete testes direcionados (`menu_layout`, `visual_webgpu`, `visual_sun`, `visual_solar_flare`, `webgpu_clouds`, `webgpu_scene` e `webgpu_atmosphere`) foram aprovados em 136,97 s; log `flare-final-tests.log`.

A conferência de profundidade usa tolerância relativa: uma tolerância absoluta em reversed-Z podia deixar passar luz por um bloqueador distante quando o plano próximo era muito pequeno. O caso novo usa plano próximo de 1e-9 e bloqueador distante, em MSAA 1x/4x; a regressão integrada passou em software, MX350 e Iris Xe após a correção (`flare-depth-software.log`, `flare-depth-high.log` e `flare-depth-low.log`).

A abertura nativa na MX350 com `--smoke-test --look-at=starbase` também passou: cinco quadros, efemérides reais, sem erros WebGPU e erro geográfico de posicionamento de aproximadamente 8,4e-8 m. Esse teste confere integração e coordenadas; não substitui a inspeção estética em diferentes horários. Log `starbase-smoke.log`.

No ensaio nativo de **1920×1080 na MX350**, as duas etapas reportaram 0,074 ms (visibilidade, com nuvens) e 0,232 ms (composição), aproximadamente 0,31 ms. Isso é uma medição da cena de teste, não uma promessa de custo para a missão inteira; não isola o tráfego adicional de salvar/recarregar o MSAA. Comando:

```sh
ALBERT_GPU=high ALBERT_GPU_TIMING=1 ./build/linux/tests/visual_test flare-check build/mission-review/flare-high-hd hd
```

Capturas em `build/mission-review/flare-software/`, `flare-high/`, `flare-low/` e `flare-high-hd/`. O teste também verifica que os reflexos não escurecem a imagem e que o aumento de um canal não passa de 16 níveis de 255 na configuração padrão.

## Continuação — integração esférica da névoa (3/11)

O limite anterior de 80 km era aplicado a partir da câmera. Em um raio rasante
ou vindo do espaço, isso podia omitir a camada marinha mais densa. A altura
linearizada também fazia um mesmo percurso atenuar a luz de maneira diferente
quando observado no sentido inverso. A transmissão deve depender da integral da
densidade ao longo do percurso, conforme as
[funções atmosféricas de referência](https://ebruneton.github.io/precomputed_atmospheric_scattering/atmosphere/functions.glsl.html).

`coastalColumn` agora integra a densidade exponencial com altura esférica. O
intervalo é recortado na camada de doze alturas de escala e dividido no ponto
de tangência, com quadratura de seis pontos por trecho. O corte despreza somente
a cauda de densidade muito baixa, em vez de descartar a parte distante do raio.
A transmissão da luz solar usa a mesma integral; o limbo ainda visível conserva
um percurso rasante quando o centro do disco está abaixo do horizonte. A
ocultação parcial continua a cargo do modelo de ar limpo existente.

Não há novas texturas, passes, LUTs ou buffers na aplicação. O cálculo passa a
ter mais operações aritméticas; isso não representa uma otimização de tempo de
GPU. Sem névoa, o intervalo tem comprimento zero e pula a quadratura. O
espalhamento adicional continua sendo a aproximação RGB existente; esta correção
não calibra aerossóis reais de Starbase nem implementa refração atmosférica.

### Regressão e verificações

O novo `coastal_optics_test` executa as funções de produção na GPU e compara os
resultados com integração Simpson independente, em dupla precisão, de 40.000
subintervalos. São treze casos: vertical, horizontal, rasante vindo do espaço,
orientação arbitrária, percurso nulo, ida/volta e transmissão solar no horizonte.

| Percurso, escala de 0,8 km | Antes | Referência independente |
| --- | ---: | ---: |
| Horizontal a 10 m, 150 km | 58,708 km | 85,282 km |
| Mesmo percurso no sentido inverso | 23,298 km | 85,282 km |
| Rasante de 1.000 km, altura mínima de 0,4 km | 0 km | 108,623 km |

São quilômetros de coluna de densidade relativa, não altura da atmosfera. Após
a correção, todos os casos passaram em MX350, Iris Xe e llvmpipe, incluindo
reciprocidade e transmissão nos três canais; tolerância de coluna de 0,2% mais
3 m. Evidências em `build/mission-review/solar-refinement/`:

- `before-high.log`: falha reproduzida antes de alterar o shader.
- `optics-adapters.log`: primeira comparação nas três GPUs.
- `solar-adapters-final.log`: treze casos e oito cenas solares aprovados nas três
  GPUs após a estabilização do cálculo do horizonte.
- `focused-tests.log`: renderização integrada, reflexos, nuvens, atmosfera e
  coluna aprovados. O teste solar inicialmente falhou por exigir saturação sob
  névoa; foi corrigido e repetido no log final acima. Não houve uma execução
  integral sem falhas dessa sequência inicial.

O teste solar agora compara o mesmo horizonte com e sem névoa, preservando a
checagem contra um disco mais escuro que o céu à frente. A saturação continua
obrigatória nos casos sem névoa adicional. No caso a 0,1° e 0,04/km, o centro
passou a RGB 236,29,23, contra 255,229,179 sem essa névoa. A névoa densa pode
ocultar legitimamente o disco; não foi acrescentado um brilho mínimo artificial.

Na cena nativa de 1280×720, MX350, qualidade alta e nuvens desligadas, o disco
foi visível acima do horizonte em 3/11/2026 às 23:40 UTC. A simulação avançou
1,50 s em 1,51 s reais, a 1×. Captura inspecionada:
`native-corrected/solar-horizon.png`; relatório em `native-corrected/results.json`.
A instância própria foi encerrada, sem erros WebGPU. A menor RAM disponível foi
5.478,7 MiB e o pico de RSS da validação foi 1.707,7 MiB. Os tempos GPU são
absolutos dessa cena (`native-corrected/starbase-review.log`), sem uma medição
anterior equivalente para atribuir custo à mudança.

O primeiro ensaio nativo (`native/`) foi descartado como conferência solar: o
script lia o campo de geometria do objeto no lugar do estado físico, apontando
para outra direção. O mesmo erro existia no antigo `solar-review-disc.py`; sua
captura não sustenta uma conclusão sobre o Sol estar oculto por nuvens. Isso não
invalida os testes isolados, que usam diretamente as posições dos objetos.

## Próximas etapas

Capturas da cena nativa na MX350 foram inspecionadas em dia (18:00 UTC), fim de tarde (23:40 UTC) e crepúsculo (23:57 UTC), em 3/11/2026, mantendo 1×. Evidências em `build/mission-review/time-controls/starbase-wide/` e relatório `starbase-results.json`. O primeiro enquadramento apontava demais para o solo; a vista foi aberta para incluir o horizonte.

Essa inspeção **não aprova ainda o resultado fotorealista**: mostra terreno sem definição, uma costura triangular no horizonte próximo e nuvens escuras com contornos recortados. A captura adicional `starbase-solar/solar-horizon.png` foi descartada como evidência solar após identificar o erro de direção no script; a repetição correta, descrita acima, mostra o disco sem nuvens. A composição com nuvens e o acabamento do terreno continuam na fila. Os testes isolados aprovados não substituem essa conferência da cena completa.

Continuar a conferência da exposição na transição entre foco solar/planetário e passagem entre a casca de nuvens e os volumes. Refinar o espalhamento fotográfico do brilho sem encobrir o veículo. Não usar um halo ou um mínimo artificial de brilho para esconder erros de transmissão.
