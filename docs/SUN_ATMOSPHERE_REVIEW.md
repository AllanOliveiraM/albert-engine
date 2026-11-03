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

## Próximas etapas

Capturas da cena nativa na MX350 foram inspecionadas em dia (18:00 UTC), fim de tarde (23:40 UTC) e crepúsculo (23:57 UTC), em 3/11/2026, mantendo 1×. Evidências em `build/mission-review/time-controls/starbase-wide/` e relatório `starbase-results.json`. O primeiro enquadramento apontava demais para o solo; a vista foi aberta para incluir o horizonte.

Essa inspeção **não aprova ainda o resultado fotorealista**: mostra terreno sem definição, uma costura triangular no horizonte próximo e nuvens escuras com contornos recortados. A captura adicional voltada ao Sol (`starbase-solar/solar-horizon.png`) não fornece um disco solar legível nesta combinação de névoa/nuvens; ainda é necessário distinguir oclusão legítima de atenuação/composição indevida. Os testes isolados aprovados acima não substituem essa verificação da cena completa. Esses pontos permanecem na fila de Sol/atmosfera, terreno e nuvens.

Continuar a conferência da exposição na transição entre foco solar/planetário e passagem entre a casca de nuvens e os volumes. Refinar o espalhamento fotográfico do brilho sem encobrir o veículo. Não usar um halo ou um mínimo artificial de brilho para esconder erros de transmissão.
