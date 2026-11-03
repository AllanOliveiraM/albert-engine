# Continuidade do terreno — revisão de 3/11/2026

## Estado e escopo

Implementação com a validação registrada abaixo. Após a etapa somente pelo código, o usuário
autorizou em 3/11 compilações, testes e demais verificações, monitorando RAM para
evitar esgotamento. Iniciada compilação com um job, testes sequenciais e proteção
que interrompe apenas os processos desta validação se a RAM disponível ficar
abaixo de 1,5 GiB por três segundos (ou 768 MiB imediatamente). Este relatório
acompanha o bloco de alterações validado; não indica conclusão de toda a fila.

Esta etapa trata a superfície terrestre, as imagens regionais e suas transições.
Não modifica a posição do pad atual, a geometria do foguete nem a captura física.
A migração para Pad 2 está fora do escopo ativo.

## Causas e mudanças

- A Terra combinava uma malha regional com o globo e uma máscara global de
  2048×1024. Em regiões pequenas, esse recorte tem resolução insuficiente para
  separar as superfícies com precisão. Com relevo, o material regional só cobria
  nós inteiros dentro dos limites; a troca de material criava uma borda rígida.
- A Terra agora usa uma superfície de quadtree também no modo simplificado.
  Imagens global e regional se misturam no mesmo fragmento, incluindo triângulos
  que cruzam o limite da região. Não há malha nem recorte de profundidade regional.
  A Lua mantém seu caminho anterior.
- O modo simplificado limita o refinamento ao nível 8, anterior à consulta de
  Copernicus no nível 9. O detalhado permite nível 12. A redução de índices por
  erro projetado continua ativa e independente da resolução das imagens.
  Nós já em construção podem terminar após a mudança; o cache não é descartado.
- O carregador de cor terrestre não baixa mais um DEM regional de 385² amostras;
  o relevo pertence ao quadtree e usa a mesma fonte/datum em toda a superfície.
- A transição dos filhos passa pelos triângulos efetivamente desenhados pelo pai,
  respeitando seu índice reduzido. Redução de LOD e chegada de alturas novas
  recebem transição de posições e normais de iluminação, com as saias acompanhando
  a superfície.
  Uma atualização recém-adotada não consome o tempo decorrido antes de existir.
- A consulta de altura ignora nós mais detalhados que estejam ocultos no cache.
  Ela continua interpolando alturas entre os posts; não é uma nova simulação
  física de contato nem inclui deslocamento de ondas no shader.
- Publicação e descarte de imagens atualizam imediatamente os materiais visíveis.
  Os materiais preservam a última qualidade apresentada durante essas operações.
- O compositor reutiliza os slots de alpha/bump, que o terreno geométrico não usa
  para recorte/normais. Assim mantém o limite de 16 texturas por estágio WebGPU.

Na revisão posterior às primeiras execuções, encontrados ainda dois problemas no
compositor: `Vector4` inicializa `w=1`, habilitando indevidamente um atlas ausente
com extensão zero; e longitude desembrulhada separadamente em cada vértice pode
interpolar a região no lado oposto do globo. O estado inativo agora tem extensão
finita e peso zero, inclusive após remover o cenário; a volta de longitude é
reconciliada no fragmento. Essas correções aguardam execução nas GPUs.

## Continuação somente pelo código

- A seleção terrestre passa a usar até 27 raios no referencial do corpo: uma grade
  5×5 com margem de tela e os eixos polares quando projetados dentro da vista.
  O limite geográfico acrescenta 20% à extensão amostrada e margem de quantização.
- Vistas inclinadas passam a pedir a região à frente da câmera. Raios descendentes
  que passam acima do horizonte incluem o ponto tangente correspondente. A caixa
  mínima do veículo amplia essa região, em vez de substituir a vista por uma caixa
  fixa sob o foguete. Fora da proximidade do veículo, a câmera deixa de carregar
  obrigatoriamente seu local de lançamento.
- Mantido um único recorte com dimensão máxima de 4096, sem atlas adicional ou
  duplicação de imagens na GPU. Em vistas muito amplas isso continua sendo uma
  escolha entre extensão coberta e resolução; a margem precisa de conferência real.
- As normais agora acompanham a transição geométrica no refinamento, na redução
  de detalhe e na chegada do DEM. A interpolação é normalizada e os buffers extras
  existem durante a transição. Medir o custo de CPU/uploads e memória temporária
  antes de aprovar; não há resultado de desempenho para esta alteração.
- Preparados `surface_region_test` (matemática de cobertura, sem GPU/rede) e o caso
  de continuidade das normais em `terrain_lod_test`. Nenhuma compilação ou execução
  ocorreu nesta continuação; as evidências abaixo são de revisões anteriores.
- Revisada a agenda das imagens regionais: mover a câmera ou deixar de olhar para
  o solo não zera mais a espera de dois minutos após falha da fonte. Trocar de corpo
  ou nível de dados e pedir nova tentativa explicitamente permite recarregar.
  Mudanças pequenas do recorte aguardam 120 ms de estabilidade, limitados a 500 ms
  de movimento contínuo, usando tempo real monotônico, independente da aceleração
  da simulação. A primeira carga continua imediata; uma imagem suficiente é mantida.
- Nova tentativa também invalida uma carga em andamento: seu resultado antigo não
  pode ser publicado depois e impedir a atualização pedida. Continua havendo apenas
  uma tarefa assíncrona de imagens regionais por vez. Preparado `regional_refresh`
  com relógio simulado, sem esperas; ainda não compilado/executado. Esse teste cobre
  os prazos, não substitui conferir cancelamento e publicação na cena em movimento.
- O quadtree descarta trabalhos ainda na fila que a vista atual já não solicita.
  Isso inclui mudanças de região, redução do nível e ocultação do terreno. A malha
  já em construção termina normalmente e o cache de malhas prontas é preservado.
  A contagem de pendências deixa de contar duas vezes os trabalhos concluídos que
  aguardam adoção. A espera inicial pelas seis faces continua ativa: os chamadores
  só ocultam/cancelam o terreno quando ele sai de uso, sem cancelar sua inicialização.
  Preparados casos com fonte bloqueada para troca de hemisfério e ocultação/retorno
  em `terrain_lod_test`. Não executados; ainda não há medição de ganho de desempenho.
- Separados os limites de refinamento e redução do quadtree: entrada em 1,25 e
  saída em 1,4 cordas além do raio do nó. Pequenas oscilações no limite deixam de
  trocar pai/filhos e reiniciar a transição a cada quadro. O limite explícito de
  nível continua prevalecendo. A banda mantém nós finos por mais distância; medir
  o custo real antes de afirmar ganho líquido de desempenho.
- Corrigida também a histerese dos índices: a tolerância de retenção se aplica a
  todos os níveis intermediários até aquele já desenhado. Antes, uma malha com
  erros semelhantes nos vários níveis podia passar de 576 índices para resolução
  completa mesmo estando dentro da banda de retenção. Preparados casos com
  oscilação de câmera, cobertura de 4π e pico diagnóstico fora dos posts reduzidos,
  verificando malhas e índices selecionados de fato. Ainda sem compilação/execução.

## Evidências anteriores à restrição de execução

Artefatos locais, ignorados pelo Git, em `build/mission-review/terrain-continuity/`:

- `before/starbase.png` e `after/starbase.png`: comparação da borda do terreno.
- `pad-high/` e `pad-simple/`: capturas próxima/ampla com foguete e torre no pad
  atual, relevo ligado/desligado, na MX350. As bordas/placas relatadas não foram
  observadas nesses quatro quadros. Isso não comprova estabilidade em movimento.
- `all-tests.log`: 48 aprovações, duas falhas em 50 testes habilitados, 413,63 s.
  Falharam `visual_webgpu` e `terrain_lod`. `starship_mission` e
  `starship_scenarios` estavam desativados e não contam como aprovações.
- `webgpu-nvidia.log`: execução adicional falhou na verificação da cor regional.
  Não apresentar essa execução como aprovação na NVIDIA.

O teste de LOD guardava os triângulos distantes e depois deixava o próprio pai
refinar antes de comparar os filhos. Foi ajustado para manter o índice observado.
A comparação das nuvens também foi ajustada: o terreno de referência deve receber
a mesma luz solar do quadro com nuvens. Esses ajustes não substituem repetir os
testes; os resultados acima são anteriores às últimas correções.

## Validação autorizada em 3/11

Artefatos desta rodada em `build/mission-review/validation-20261103/` (ignorados
pelo Git), com logs e arquivos `*.memory.json` do monitor de RAM.

- Build completa com um job aprovada. A primeira tentativa revelou um acesso
  `const` incompatível com a API de uniformes em um teste novo; corrigido.
- Os cinco testes direcionados (`free_camera_motion`, `regional_refresh`,
  `visual_terrain`, `terrain_lod`, `surface_region`) passaram em 6,04 s.
  Incluem histerese de malhas/índices e descarte da fila antiga, não só compilação.
- A suíte completa rodou sequencialmente em 595,00 s: 52 aprovações entre 53
  habilitados, uma falha em `visual_webgpu`. Os dois testes longos de missão já
  desativados (`starship_mission`, `starship_scenarios`) não foram executados.
- A falha visual era a iluminação do cenário de teste: preenchimento ambiente
  iluminava o chão, mas não representava luz atmosférica para a casca de nuvens.
  Valores medidos: chão com preenchimento 197, somente solar 168, nuvem opaca 225.
  Mantida a exigência de contraste de 40 e a comparação entre quadros igualmente
  iluminados. Após a correção, `visual_webgpu` passou; repetido via
  `ctest --rerun-failed` após a suíte, passou novamente em 32,33 s. Portanto os
  **53 testes habilitados foram aprovados entre a suíte e o reteste**, não em uma
  única execução integral sem falhas.
- `visual_test terrain-check` e `gpu_scene_test` passaram nas GPUs NVIDIA GeForce
  MX350 e Intel Iris Xe (TGL GT2), além da cobertura de software na suíte. Na Intel,
  corrigida a comparação inteira do contraste mínimo da areia para aceitar
  exatamente 25 níveis (170 contra 145); as verificações de água, vegetação,
  estradas, recorte regional e descarte continuam ativas.
- Em cada teste de descarte nas GPUs físicas, três ciclos terminaram com zero
  texturas, bytes e grupos de materiais retidos. As mensagens sobre índices e
  imagens inválidos são entradas intencionais de regressão, rejeitadas antes do
  envio à GPU; não são erros WebGPU.
- Menor RAM disponível observada até esta etapa: 3.596,6 MiB na primeira build.
  A retomada da build chegou a 1.479,3 MiB de RSS da árvore de processos;
  suíte/reteste ficaram abaixo de 1.302 MiB nas amostras. Nenhuma proteção foi
  acionada. RAM disponível e swap são medições do sistema inteiro, incluindo
  outras cargas do usuário; não atribuir essas variações apenas ao Albert.

- A instância nativa MX350 completou os 180 s programados (196,29 s com carga
  inicial), dois ciclos de zoom/deslocamento/menu e oito recargas regionais, sem
  crash ou erro WebGPU. O cache nos dois afastamentos voltou a 18 texturas e
  188,6 MiB, sem crescimento entre ciclos. RSS amostrado da árvore: 1.339,2 MiB;
  menor RAM disponível: 7.017,8 MiB. Pico de 791 MiB no dispositivo NVIDIA inteiro,
  não uma atribuição exclusiva de VRAM ao Albert. Quatro capturas em `earth-high/`.
- Capturas nativas próximas/amplas do pad atual com o veículo: `pad-high/`,
  qualidade/relevo ligados, e `pad-simple/`, ambos desligados. Nos quadros
  inspecionados não há fragmentação do chão nem uma placa regional sobreposta.
  A geometria permanece alinhada às estruturas; as cores de cobertura continuam
  estilizadas, e estas capturas não comprovam realismo fotográfico ou estabilidade
  de todas as transições em movimento. A camada noturna também revela limites
  de resolução na aproximação; não usar as capturas de diagnóstico no README.
  A instância detalhada encerrou pelo script (41,64 s), pico RSS 1.686 MiB,
  sem pressão de RAM. O invólucro da primeira rodada simplificada terminou com
  sinal 143 após produzir as capturas, sem erro no log da aplicação; não conta
  como aprovação de encerramento. A repetição em `pad-simple-retry/` terminou
  normalmente em 41,93 s, com as duas capturas, pico RSS 1.453,5 MiB e pelo menos
  5.342,8 MiB de RAM disponível. Nenhuma instância de teste ficou em execução.

### Roteiro de conferência

1. Compilar todos os alvos necessários, limitando os jobs conforme a carga
   disponível. Não iniciar outra compilação Ninja em paralelo.
2. Rodar `regional_refresh`, `surface_region` e `terrain_lod`, incluindo vistas inclinadas, horizonte,
   polos, mudança de data, pai com índice reduzido, cobertura sem buracos, limite
   de refinamento, cache oculto, fila após salto da câmera/ocultação, histerese
   de pai/filhos e dos índices e continuidade de alturas/normais.
3. Rodar `visual_terrain` nas três GPUs: software, NVIDIA MX350 e Intel Iris Xe.
   Exercita o compositor real com/sem cenário, mistura nas bordas, região pequena
   na mudança de data, ausência de imagem no antípoda, liberação e estradas.
4. Repetir `visual_webgpu` e a suíte habilitada; avaliar cada falha antes de seguir.
   Preferir execução sequencial para não disputar CPU/GPU com outras cargas.
5. Conferir a cena nativa com relevo/qualidade ligados e desligados, câmera próxima,
   ampla, rasante e orbital, movimento/zoom, troca de imagem e chegada do DEM.
   Incluir veículo e pad atual, sem migrar para Pad 2. Conferir recarga em movimento
   contínuo, retenção da imagem anterior, cancelamento ao mudar de corpo e nova
   tentativa durante uma carga/atualização da biblioteca de imagens.
6. Medir memória/custo em condições comparáveis e verificar que nenhum pipeline
   excede o limite de texturas nem gera erro WebGPU. Não atribuir ganho percentual
   a contagens de nós ou capturas com ângulos/cargas diferentes.
7. Revisar o diff final e as anotações. Com a validação aprovada, realizar o commit
   e o push solicitados e confirmar a revisão remota publicada.

Comandos usados nesta rodada (o reteste substituiu repetir toda a suíte após
uma correção exclusivamente no cenário de teste):

```sh
/usr/bin/ninja -C build/linux -j1
LP_NUM_THREADS=2 ctest --test-dir build/linux --output-on-failure -R '^(free_camera_motion|regional_refresh|surface_region|terrain_lod|visual_terrain)$' -j1
ALBERT_GPU=high ./build/linux/tests/visual_test terrain-check
ALBERT_GPU=low ./build/linux/tests/visual_test terrain-check
LP_NUM_THREADS=2 ctest --test-dir build/linux --output-on-failure -j1
LP_NUM_THREADS=2 ctest --test-dir build/linux --rerun-failed --output-on-failure -j1
```

## Limites para fechar o item

A continuidade geométrica, os índices, as normais e a cobertura amostrada passaram
nos testes descritos; zoom/menu e as vistas do pad também foram conferidos.
Ainda falta ampliar a inspeção em movimento rasante, voo com vista auxiliar e
distorção, e medir tempos de quadro em condições comparáveis. O nível geométrico e o DEM de 30 m não aumentam
a resolução das fotografias de origem. A mistura remove a borda, mas não cria
detalhes ausentes nas imagens. A cobertura em voo continua usando uma região
principal, agora selecionada pela vista amostrada; conferir a margem diante da
câmera e o comportamento no horizonte, incluindo distorção e vista auxiliar,
antes de considerar atendido o pedido de nitidez/cobertura em todos os ângulos.
