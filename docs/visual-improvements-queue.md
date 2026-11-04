# Backlog consolidado: missão Starship, V3 e visuais

Este documento reúne os pedidos ainda relevantes da conversa e serve como brief para uma execução futura. **Não significa que os itens abaixo já foram implementados.** Antes de mudar código, audite a árvore atual e confira cada item: várias solicitações de interface e missão já aparecem como implementadas no README e podem ter sido concluídas durante o trabalho anterior.

## Prioridades por relevância — 3/11/2026

### Ordem ativa de implementação — atualização do usuário

**Bloqueador de CI antes de seguir a fila:** as execuções Windows de `7b4768a`
falharam nos testes gráficos; Linux passou. Investigação, mitigação do trabalho
acumulado no adaptador CPU e diagnóstico ampliado em
[WINDOWS_GPU_CI_REVIEW.md](WINDOWS_GPU_CI_REVIEW.md). A aprovação nas três GPUs
Linux não substitui a validação Direct3D/WARP. Não considerar resolvido antes
da nova execução Windows.

Continuar pelos itens que ainda exigem implementação, sem interromper a fila
para pedir confirmação a cada bloco. Testar as alterações com monitoramento de
RAM e manter as evidências e limitações registradas. Pendências exclusivamente
de validação da tabela histórica abaixo não entram nesta sequência.

1. Sol e atmosfera: coluna esférica de névoa corrigida e testada nas três GPUs;
   Sol conferido na cena nativa. Restam limites do modelo RGB e acabamento final.
2. Terreno: nitidez das imagens e acabamento das cores (em andamento).
3. Precisão distante: tremor da câmera e órbitas deslocadas em Plutão e além.
4. Nuvens: realismo, definição no zoom e estabilidade em movimento.
5. Efeitos: plumas, queima de pouso, reentrada e dilúvio — antecipados pelo usuário,
   antes do trabalho de hardware V3 (era o item 8 da lista reduzida).
6. Satélites terrestres: pesquisa e seleção de ISS, satélites e constelações —
   antecipados pelo usuário para logo depois dos efeitos.
7. Booster e Ship V3: motores, suportes, tubulações, folgas e escudo térmico.
8. Torre e SQD: formato/conexões reais, tubulações e sobreposições.
9. Áudio: mixagem da tank farm e ventilações.
10. Órbitas: oclusão indevida e aparência das linhas.
11. README: novas capturas com melhores ângulos e qualidade máxima.

Os números desta sequência se referem à lista reduzida de implementação. A
tabela de 14 prioridades e as seções detalhadas preservam o histórico completo.

**Execução autorizada novamente em 3/11:** o usuário liberou testes e demais verificações, com monitoramento de RAM para evitar esgotamento. Compilar/testar com baixa concorrência, observar RAM disponível e swap e interromper apenas os processos desta validação se houver pressão de memória. Commit/push continuam pedidos, após a validação aprovada.

Ordem atualizada conforme pedido do usuário: estabilidade em primeiro, Sol e atmosfera em segundo, interface e tempo em terceiro. Os demais itens seguem por impacto no uso, gravidade, dependências e evidências atuais. Recursos novos e imagens de divulgação ficam depois das correções. Os detalhes de todos os pedidos foram preservados nas seções seguintes.

| Ordem | Prioridade e trabalho | Estado / próximo critério |
| --- | --- | --- |
| 1 | P0 — Crashes WebGPU, índices e memória no zoom | Teste integrado de 180 s por GPU aprovado em MX350 e Iris Xe, oito cargas reais cada; pico NVIDIA de 791 MiB. Ampliar a cobertura para voo/vista auxiliar e redimensionamentos. |
| 2 | P2 — Sol e atmosfera | Corrigidos disco escuro e composição do ar; sete cenários com capturas antes/depois. Reflexos discretos aprovados nas três GPUs e na suíte integrada; falta refinamento e conferência estética nas cenas reais. |
| 3 | P2 — Interface e tempo | Slider global corrigido até um ano/s; missão com slider, retomada da velocidade e costa sem espera durante saltos. Cinco testes aprovados; validar demais marcos e interação visual dos painéis. |
| 4 | P0 — Funcionamento e segurança da missão | Capturas nominais restauradas; completar validação de retorno orbital e falhas. Melhorias somente no pad atual. Pad 2 fora do escopo por decisão do usuário. |
| 5 | P1 — Terreno: piscadas, sobreposição, costura e custo | Bloco de continuidade/LOD validado: 53 testes habilitados entre suíte/reteste, GPUs físicas e cena nativa. Restam nitidez das fontes, vistas auxiliar/distorcida e medição comparável de custo. |
| 6 | P1 — Câmera e precisão de coordenadas | Inércia de translação e suavização do mouse antecipadas pelo usuário; validar sensação em uso prolongado, ancoragem à Terra e estabilidade ao focar Plutão. |
| 7 | P1 — Nuvens no movimento e no zoom | Resolver instabilidade e perda de definição antes do refinamento de aparência; validar resolução/memória e os três modos. |
| 8 | P1 — Booster e Ship V3 | Bocais e domo parcialmente corrigidos; reconstruir corpos dos motores, suportes, tubulações, escudo e folgas com referências. |
| 9 | P1 — Torre e SQD | Conexões contínuas, formato real, topo sem sobreposição e peças junto à Ship. |
| 10 | P2 — Áudio e sincronização | Ventilações audíveis e eventos coerentes com distância, pausa, saltos e replay. |
| 11 | P2 — Plumas, reentrada e dilúvio | Forma e movimento realistas, boa leitura do veículo e custo controlado. |
| 12 | P2 — Órbitas e trajetórias | Oclusão correta, legibilidade e distinção entre passado, previsão e plano. |
| 13 | P3 — Pesquisa de satélites terrestres | Recurso novo: fonte/época/propagação/cache e seleção explícita, sem catálogo inteiro por padrão. |
| 14 | P3 — Auditoria final e README | Só após validar as cenas: desempenho, melhores ângulos e imagens reais na qualidade máxima. |

**Escopo confirmado pelo usuário em 3/11:** seguir pelo item 5 agora. O item 4 abrange somente melhorias e validação do pad atual. A migração para Pad 2 não integra a fila ativa; suas notas e worktree ficam apenas como histórico, sem reintegração programada.

Concluir a revisão do diff por data, incluindo o commit de 2/11 e as alterações locais de 3/11; registrar defeitos encontrados e validações, sem considerar uma compilação bem-sucedida prova de correção visual ou física.

## Direção obrigatória

- Pesquisar e estudar a configuração **Starship V3** antes de modelar ou alterar qualquer componente. Identificar a variante e a data das referências; não misturar V1/V2, protótipos, renders conceituais e V3.
- Tratar a foto de referência do lado inferior do Booster enviada pelo usuário nesta conversa como referência visual direta. Ela mostra a relação entre saia do tanque, estrutura anular, proteção/interstícios e conjunto de motores. Reavaliar as referências públicas atuais antes de assumir que algum detalhe dessa foto permanece igual em todas as V3.
- Dar preferência a fontes primárias da SpaceX e fotografias nítidas da configuração correta. A página oficial de apresentação da V3 é um ponto de partida: [SpaceX — Starship V3](https://new.spacex.com/updates). Fotos de campo só valem após conferir veículo, variante, data e ângulo.
- Manter um registro curto por detalhe: referência, componente, localização, orientação, dimensões/proporções conhecidas ou estimadas e grau de confiança. Se uma parte não estiver visível ou documentada, não apresentá-la como fato; evitar acrescentar peças plausíveis, mas inventadas.
- As coordenadas físicas, eixos, pivôs, contatos e relações entre peças precisam acompanhar a geometria visível real. Não corrigir a imagem deslocando apenas a malha se isso deixar coordenadas físicas incompatíveis.
- Preservar desempenho, LOD e estabilidade WebGPU. Comparar qualidade alta/baixa, próximo/longe e diferentes ângulos. Corrigir qualquer triângulo cujo índice ultrapasse a contagem de vértices.

## 1 · P0 — Estabilidade WebGPU: índices e memória no zoom

O log de 2/11/2026 reproduz esgotamento de memória no `wgpuDeviceCreateTexture` após várias mensagens “Detalhes locais carregados: earth”. A MX350 tem 2 GB; as regiões transferidas chegam a 4096×4096 e algumas novas texturas são criadas enquanto cópias anteriores e bind groups ainda as mantêm vivas. O crash final em `wgpuQueueSubmit` é consequência das views inválidas depois da primeira falha de alocação.

**Avanço em 3/11 após reordenar a fila:** reproduzida e corrigida uma brecha na liberação: materiais ocultos ainda mantinham views de versões antigas, mesmo depois do descarte da cópia atual. A invalidação agora usa a identidade da textura, cobrindo todas as versões. O teste novo faz três ciclos com imagens de 4096×4096, descarte de upload pendente, retorno à imagem e destruição; passou em llvmpipe, NVIDIA MX350 e Intel Iris Xe, com zero grupos retidos após cada descarte e sem erros de validação WebGPU. O diagnóstico agora distingue bytes da cópia atual e da pendente. Os testes de cena/índices também passaram nesses três adaptadores; os quatro testes de integração visual/oceano/nuvens/atmosfera passaram em software (54,53 s). Isso não encerra a validação do crash no programa completo. Evidências e reprodução: [GPU_TEXTURE_LIFETIME.md](GPU_TEXTURE_LIFETIME.md).

- Liberar imediatamente a cópia GPU e os bind groups quando uma textura threepp recebe `dispose`, em vez de depender do timeout geral do cache (implementado e validado nos testes e na reprodução integrada em MX350/Iris Xe).
- Reprodução integrada concluída em MX350 e Iris Xe: oito cargas regionais por adaptador, 180 s de câmera/menu após a carga inicial, sem crash; cache retorna a 149,4 MiB / 11 texturas. MX350: pico de 791 MiB no dispositivo inteiro. Iris Xe: pico de RSS de 1.421,1 MiB, sem medição de VRAM. Ampliar a cobertura para voo/vista auxiliar, resize e sessões mais longas.
- Se a pressão persistir, limitar a memória residente/temporária dos tiles e o upload simultâneo sem reduzir a nitidez da região ativa; não ocultar o crash apenas usando a GPU integrada.

## 2 · P2 — Sol, atmosfera e reflexos

**Avanço em 3/11:** reproduzido o disco escuro com os shaders reais. A emissão agora usa radiância, a composição opaca conserva a luz espalhada à frente do Sol e a inspeção do Sol usa exposição própria. Sete cenários renderizados antes/depois, incluindo dia, horizonte, névoa densa, ocultação pela Terra, limbo orbital, espaço e inspeção. Fontes, resultados e limites em [SUN_ATMOSPHERE_REVIEW.md](SUN_ATMOSPHERE_REVIEW.md). Na continuação, implementados reflexos de lente discretos com oclusão e transmissão das nuvens; testes isolados aprovados nas três GPUs, em MSAA 1x/4x e 1080p na MX350. Build integrada e sete testes direcionados aprovados (136,97 s). Corrigida também a tolerância de profundidade para bloqueadores distantes; falta concluir a conferência estética nas cenas reais.

- Continuar a melhorar a luz solar na Terra e a interação da luz com atmosfera, horizonte e nuvens, conferindo dia, terminador e crepúsculo.
- Investigar o pequeno disco solar escuro/preto relatado. Conferir transmissão atmosférica, exposição, composição e oclusão, no solo e em órbita, com o Sol sobre o horizonte e parcialmente encoberto.
- Reflexos de lente sutis adicionados para o Sol, controlados por posição, exposição, tamanho angular, atmosfera/nuvens e oclusão; controle persistente em Ajustes → Efeitos. Testes de bloqueio total/parcial aprovados nas três GPUs. Validar em cenas reais e na transição entre casca e volume de nuvens; preservar a leitura da cena.

## 3 · P2 — Interface de missão, tempo e interferências: auditar o estado atual

**Avanço em 3/11:** controles de atmosfera, relevo, nuvens, luzes, fronteiras e órbitas reunidos em **Cena → Terra e céu**, removendo a seção duplicada de Ajustes. Atualizados os caminhos no README e os textos de ajuda da câmera livre. Campos UTC agora usam entrada direta, ano em linha própria e demais campos em duas colunas; removidos os botões que comprimiam o texto. A largura do popup acompanha a escala da fonte. Teste de layout aprovado em nove tamanhos/escalas. Incluído **Cena → Locais → Starbase · vista geral**, também disponível por `--look-at=starbase`, preservando horário e taxa. Abertura nativa na MX350 aprovada com verificação geográfica; não altera a posição do pad nem a física da missão. Ainda falta conferir a interação do atalho e os demais fluxos da missão.

**Continuação em 3/11:** reproduzido um crash potencial: o slider oferecia um ano/s e o relógio aceitava só uma semana/s. UI e API agora usam o mesmo limite (31.557.600×), inclusive na digitação com Ctrl+clique. Novos testes exercitam os limites digitados dos dois sliders, data/hora preservando pausa/taxa/sentido e parada nos extremos de 1900–2100. Na MX350, a cena nativa confirmou 1× em três horários, avanço/retorno no máximo e pausa, com capturas em `build/mission-review/time-controls/`. O enquadramento de Starbase foi aberto para incluir horizonte; terreno e nuvens ainda limitam a aparência.

Na missão, os botões de velocidade foram trocados por slider logarítmico de 0,5× a 60×; pausar/retomar conserva a seleção, e os botões se empilham quando não cabem lado a lado. Estado de reprodução sincronizado entre painel e computador de voo. O teste integrado avança até a decolagem, retorna a 1×, recusa marco passado e confere o painel em nove tamanhos/escalas. Cinco testes direcionados aprovados (10,15 s): layout, missão embarcada, RPC, relógio simulado e provedores. Evidências em `time-controls/final-tests.log`.

**Pedido adicional — costa da Ship:** os 60× eram um multiplicador, e não o limite absoluto da aceleração orbital. Durante o avanço explícito até um marco, a costa orbital e o trecho após a saída de órbita agora executam sem esperas artificiais, preservando os passos de orientação/integração. O custo físico ainda limita a velocidade efetiva. Testados ausência de atraso programado, retorno à cadência normal sem dívida de tempo, aceleração normal sem salto, fases ativas limitadas e pausa/cancelamento. Não altera geometria, pad, controle ou dinâmica física. Ainda falta medir um salto orbital completo na cena real e validar os marcos de reentrada/pouso; o teste curto até a decolagem não substitui esses casos.

O README já descreve uma missão Starship incluída no Albert, controles no horário para velocidade/saltos a marcos, órbitas dos demais corpos ocultadas por padrão e vista de trajetória na Terra. Também descreve o painel de interferência à direita e fechado por padrão. Auditar esses fluxos antes de mexer; corrigir somente falhas ainda reproduzíveis.

- Confirmar que apenas a missão Starship está no menu de missões incluídas e que a missão é compilada/distribuída com o programa sem passos manuais de RPC.
- Confirmar o seletor de tempo da missão: pausar/retomar, velocidade e destinos de salto claros, integrados ao menu de tempo, sem botões de pular reentrada espalhados ou duplicados.
- Confirmar que os saltos oferecem os marcos pertinentes (lançamento, separação, retorno/captura do Booster, reentrada e pouso) e deixam explícito o destino e o efeito no tempo.
- Costa da Ship durante salto: remover a espera de relógio sem pular física (implementado/testado no pacer); medir o ganho no voo real, inclusive com aceleração orbital normal desligada, e conferir a devolução a 1× na reentrada.
- Confirmar que o painel de interferências está compacto, encaixado à direita, fechado inicialmente e sem o campo de texto redundante mencionado pelo usuário. Testar abertura, fechamento e layout em diferentes tamanhos de janela.
- Reorganizar o seletor de data/hora UTC para não comprimir/cortar os campos (especialmente o ano). Aplicar data e hora deve preservar a taxa escolhida e permitir iniciar no pôr do sol seguindo em tempo normal, sem congelar a iluminação.
- Starbase incluída no menu de locais, com vista geral geograficamente ancorada; conferir interação do menu, retorno de foco distante e enquadramento durante a missão.
- Reunir as opções de “Cena e Terra”, hoje dentro de Ajustes, no menu Cena principal; atualizar textos e documentação para a localização final.
- Substituir os botões de distorção temporal por slider de curva logarítmica com alcance ampliado. Alteração local existente até um ano por segundo; validar pausa, tempo normal, valores extremos e controles específicos da missão.

## 4 · P0 — Missão e segurança no pad atual

**Regressão confirmada em 3/11:** uma compilação separada com os exemplos do commit `7966519` capturou o Booster nominal aos 91,54 s e o manteve nos braços pelos 30 s pedidos pelo teste, com 0,024146 m/s. A migração local para a Pad 2 deixou de obter capturas estáveis. A integração principal foi restaurada ao mesmo commit nos três headers da missão, preservando os demais ajustes locais. A build de `AlbertEngine` e `starship_test` passou. A árvore principal restaurada reproduziu a captura do Booster aos 91,54 s e a captura da Ship no teste de queda aos 137,9 s, ambas mantidas por mais 30 s (0,024146 m/s e 0,034 m/s, respectivamente). A suíte CTest terminou com 47 testes aprovados e dois testes longos desativados (305,07 s). O teste de queda da Ship não substitui sua varredura orbital completa.

### Histórico fora do escopo ativo: experimento da Pad 2

Não executar os itens históricos abaixo nesta fila. A migração, suas correções e os testes novos foram preservados na worktree local `/unreal/allan/albert-engine-pad2-review`, com diagnóstico em `docs/PAD2_REVIEW_STATUS.md`. Ela ainda NÃO deve substituir a missão principal:

- Corrigir todas as projeções de posição, velocidade, aceleração e altura segura para o eixo dos trilhos. Foram encontrados pontos ainda usando norte/leste geográficos depois de girar o corredor.
- Manter o sentido do eixo transversal coerente com o sinal de abertura: os braços da migração abriam cruzando a linha central em X. A correção experimental ajusta física, previsão e desenho em conjunto, preservando a posição do SQD.
- Definir o ponto desejado de captura dentro do trecho útil dos trilhos. A migração mirava praticamente sua extremidade; o experimento de rebase ao meio dos trilhos ainda não validou estabilidade nem aborto e não é uma medição do hardware real.
- Calcular e reportar deslocamentos de captura no referencial dos trilhos; os diagnósticos ainda confundiam coordenadas geográficas com distância ao eixo de captura. Manter as distâncias à torre, ao ponto de captura e à origem da montagem distintas.
- Rever a exceção do monitor que ignorava os braços com o CG até 3 m fora da linha. As folgas do casco, sua inclinação, o fechamento e a sustentação pelos pinos precisam ser avaliados junto da geometria física.
- Rever aproximação lateral, estimativa de propelente/tempo e rota de aborto antes de reintegrar. Não aumentar propelente ou desligar proteções só para obter capturas.
- Exigir captura nominal de Booster e Ship mantida por 30 s, varreduras de vento/falhas/queda/retorno orbital e validação visual. Os testes `starship_mission` e `starship_scenarios` continuam desativados no CMake e não contam como aprovações.
- Na versão intermediária da migração, 48 testes habilitados passaram, mas o sweep tinha 41 desvios seguros e zero capturas. A varredura adicional da Ship encontrou 2 cenários inseguros em 17. Isso demonstra que aprovação de desvio seguro não comprova funcionamento da missão. As alterações experimentais posteriores de abertura/ponto de captura ainda apresentaram falha no aborto com vento/motor 1.

## 5 · P1 — Terreno detalhado integrado ao entorno

**Próximo bloco de nitidez — análise inicial:** as imagens planetárias já pedem
anisotropia 8 e o WebGPU respeita essa configuração com mipmaps. Não atribuir o
borrado à ausência desse filtro. O mosaico terrestre empacotado é preparado a
86400×43200, cerca de 500 m por pixel no equador; um recorte regional de 4096
não cria detalhe ausente na fonte. Separar essa limitação da resolução perdida
ao cobrir uma vista ampla com um só recorte. Avaliar fonte regional medida e
distribuição do orçamento pela vista antes de aumentar texturas ou acrescentar
ruído artificial. Preservar as correções de continuidade/LOD já verificadas.

**Etapa de continuidade em 3/11 — ainda não encerrada:** imagens regionais e globais da Terra agora usam a mesma malha, com mistura nas bordas. O modo sem relevo detalhado mantém a curvatura e limita o refinamento ao nível 8; o modo detalhado vai ao 12. Retirados a máscara/patch separados da Terra e o download regional duplicado de DEM. Ajustadas as transições de refinamento, redução de LOD e atualização de alturas; a consulta do chão ignora nós ocultos no cache. Não altera pad, geometria do foguete ou física de captura.

Antes da restrição de execução, foram inspecionadas capturas nativas da vista de Starbase e do pad atual, próximo/amplo, com relevo ligado/desligado. A suíte daquela revisão terminou com **48 de 50 testes habilitados aprovados**, duas falhas (`visual_webgpu`, `terrain_lod`) e dois testes longos desativados; portanto não foi aprovada integralmente. A execução adicional na NVIDIA falhou na imagem regional. Revisão posterior corrigiu estados de atlas sem cenário com extensão zero, interpolação de longitude entre vértices, publicação/liberação de texturas e o teste que comparava LODs diferentes. Também ajustada a comparação de nuvens para manter a mesma iluminação. **As últimas mudanças ainda não foram compiladas nem testadas.**

**Continuação sem execução:** seleção das imagens terrestres pela vista real amostrada, com margem e inclusão do horizonte/polos; a caixa do veículo passa a ser uma área mínima adicional. Mantido um recorte de até 4096, sem atlas extra na GPU. Acrescentada transição das normais junto às posições para tratar mudanças abruptas de iluminação. Preparados testes de cobertura e continuidade de normais, ainda não compilados/executados. A nitidez efetiva, o custo e o comportamento na vista auxiliar/distorção seguem pendentes de validação autorizada.

**Carregamento em movimento — revisão de código:** mudanças da câmera não zeram mais a espera após falha da fonte. Pequenas mudanças do recorte são agrupadas por 120 ms, com limite de 500 ms durante movimento contínuo; primeira carga imediata e uma única tarefa assíncrona mantidas. Nova tentativa invalida também o resultado em andamento para que dados antigos não impeçam a atualização solicitada. Preparado teste determinístico dos prazos (`regional_refresh`), ainda sem execução. Conferir retenção da imagem anterior, cancelamento, recarga em voo e custo real após autorização.

**Fila do relevo — revisão de código:** trabalhos ainda não iniciados para uma vista abandonada deixam de ocupar a fila; o trabalho em execução e as malhas prontas permanecem. A ocultação e a redução de detalhe também retiram trabalhos desnecessários. Diferenciada a espera inicial pelas faces da desativação real do terreno. Preparados dois casos de regressão para salto entre hemisférios e ocultação/retorno; ainda não compilados ou executados, sem ganho percentual de desempenho alegado.

**Estabilidade do LOD — revisão de código:** refinamento e redução agora usam limites separados para evitar alternância de malhas e reinício das transições com pequenas oscilações da câmera. Corrigida a retenção dos índices reduzidos nos níveis intermediários, que podia provocar retorno desnecessário à resolução completa. Preparados testes que inspecionam as malhas, a cobertura e os índices reais. Mudanças ainda sem compilação/execução; conferir piscadas, custo e nitidez em movimento após autorização.

**Validação autorizada — rodada atual:** build com um job e cinco testes direcionados aprovados. A suíte sequencial aprovou 52 de 53 testes habilitados; a falha visual de nuvens foi corrigida no cenário de iluminação e o reteste passou, completando as 53 aprovações entre suíte e reteste. `visual_terrain` e `gpu_scene_test` também aprovados na MX350 e Iris Xe, com três ciclos de descarte sem texturas/grupos retidos. Menor RAM disponível até esta etapa: 3.596,6 MiB; proteção de memória não acionada. Conferência nativa em andamento. Os dois testes longos já desativados de missão não contam como aprovados. Detalhes e limites em [TERRAIN_CONTINUITY_REVIEW.md](TERRAIN_CONTINUITY_REVIEW.md).

**Conferência nativa concluída:** MX350 completou os 180 s de diagnóstico, oito recargas e retorno à mesma base de cache nos dois ciclos, sem crash/erro WebGPU. Conferidos pad/veículo em vistas próximas/amplas, com relevo/qualidade ligados e desligados; a repetição do modo simplificado encerrou normalmente. Pico RSS amostrado do pad detalhado: 1.686 MiB; dispositivo NVIDIA inteiro: 791 MiB no diagnóstico da Terra. Sem pressão de RAM ou instâncias restantes. As cores procedurais e a resolução das fontes, inclusive luzes noturnas, ainda limitam o realismo; fotos do README, voo/vista auxiliar, distorção e comparação de tempos de quadro seguem na fila.

Reprodução, causas, limites e plano de validação em [TERRAIN_CONTINUITY_REVIEW.md](TERRAIN_CONTINUITY_REVIEW.md). Não marcar o item concluído nem publicar como validado antes de passar a validação autorizada. A nitidez da imagem-fonte e a cobertura antecipada em voo continuam exigindo avaliação própria.

A última captura mostra uma área de terreno detalhado pequena, quadrada/retangular e aparentemente destacada sobre a paisagem desfocada, com uma borda azul escura. Ela precisa parecer continuação natural do terreno ao redor, e não um tile pousado sobre o globo.

- Descobrir qual camada produz a borda (imagem regional, malha de relevo, máscara, costa/água, origem local ou sobreposição com o terreno global) e corrigir a causa, não mascarar o quadro com uma peça decorativa.
- Unificar datum, altitude, geóide, escala, coordenadas e normais do relevo local com o terreno global. Evitar degraus, flutuação, frestas, interseção dupla, diferença de cor/iluminação e faixa azul que não corresponda à costa real.
- Fazer a transição visual de textura e resolução desaparecer gradualmente sem borrar o terreno próximo. Manter a costa e as lagoas com forma e cor coerentes com o entorno.
- Corrigir os casos relatados de chão borrado mesmo em alta qualidade, briga/pisca de terreno ao ativar relevo detalhado e fragmentação do terreno em placas quando o relevo detalhado está desligado. A troca de LOD/qualidade não pode fazer o chão piscar ou abrir buracos.
- Reavaliar custo do relevo detalhado: alta nitidez perto da câmera, redução de detalhe distante com transição estável e sem custo desnecessário. Mostrar erro/LOD coerente em vista orbital, aproximação, plano rasante e solo.
- Validar com a Ship/Booster no quadro, para comprovar que o terreno e o veículo partilham escala, posição e horizonte.

## 6 · P1 — Câmera e precisão de coordenadas

**Pedido adicional e implementação em 3/11:** movimento suave com inércia, antecipado nesta etapa por solicitação do usuário. Aceleração/frenagem graduais e suavização curta do mouse, usando tempo de quadro da apresentação; mantidos WASD horizontal, Espaço/Shift vertical e Ctrl para acelerar. A velocidade é retida nos eixos ortonormais do corpo em rotação, independente da direção do olhar e da origem flutuante. Contato com o chão remove a componente de velocidade para dentro do solo; abrir a interface, perder foco da janela, mudar de câmera/local ou trocar de corpo limpa a inércia. Teste novo `free_camera_motion`: aceleração, soltura com parada finita, inversão de direção, equivalência em 30/60/144 fps, mudança de referencial, contato tangencial e preservação do ângulo total do mouse. Evidências em `build/mission-review/time-controls/free-camera-*.log`. Ainda falta conferir a sensação de navegação e colisões em trajetos prolongados na cena real; os problemas de precisão em Plutão continuam separados e pendentes.

**Complemento solicitado:** Q/E guinam em torno da vertical local, com cadência independente de FPS. Ao entrar pelo comando de câmera livre, o mouse fica capturado e olha sem clique, com movimento bruto quando suportado pelo GLFW. Esc libera o cursor; clicar na cena ou no botão Capturar mouse retoma a captura; F, um segundo Esc ou o botão de saída encerram o modo. Dica fixa no canto inferior direito, substituindo Restaurar zoom nesse modo. Perda de foco solta o cursor e a recaptura descarta sua primeira amostra para evitar saltos. A vista geográfica de Starbase inicia com cursor livre para observação. Testes de deltas/recaptura e Q/E em 30/60/144 fps aprovados. Build aprovada; teste nativo anterior da câmera e `visual_controls` também passaram. Interação prolongada com mouse físico, alternância de janela e escala do painel continuam na validação manual.

Painel de ajuda também capturado e inspecionado na cena nativa da MX350, em 1280×720: `build/mission-review/time-controls/free-camera-help/free-camera-help.png`. A entrada por F ou pelo ajuste fecha o menu; F não interrompe a digitação de campos de texto.

- A câmera livre relatada fugindo com a rotação da Terra precisa permanecer ancorada ao referencial escolhido. Verificar movimentos prolongados e troca de foco sem deriva causada pela rotação terrestre.
- Movimento suave com inércia (implementado): conferir ajuste de aceleração/frenagem, mouse, boost, scroll e paradas ao abrir a interface em solo e espaço, sem substituir a ancoragem geográfica por velocidade no referencial errado.
- Refazer a navegação da câmera livre com controles de primeira pessoa: mouse para olhar e WASD para andar/deslocar como no Minecraft; W/S devem avançar/recuar no plano tangente ao corpo e A/D deslocar lateralmente, sem a inclinação do olhar transformar o movimento horizontal em subida/descida. Usar Espaço para subir, Shift para descer e Ctrl para acelerar. Capturar o mouse apenas a pedido e permitir soltá-lo para interagir com o painel. Preservar controle de velocidade pelo scroll e exibir as teclas no painel; verificar movimento em solo, perto da superfície e no espaço.
- Durante voos em tempo real, ampliar a cobertura de imagens para incluir o campo de visão e uma margem de antecipação ao redor da câmera/veículo, evitando que o terreno detalhado apareça somente sob o trajeto já percorrido. A seleção pela vista e suas margens foram implementadas nesta continuação, incluindo a área mínima do veículo; ainda falta compilar e inspecionar em voo, medir nitidez/custo e conferir vista auxiliar/distorção dentro do orçamento de memória/resolução do relevo.
- Investigar perda de precisão ao focar corpos muito distantes (Plutão e além): órbitas aparentando deslocamento e tremor da câmera ao alternar o foco. Auditar origem local/rebase, precisão dupla até a conversão final para GPU, posições e velocidades interpoladas, caminhos orbitais, transições e coordenadas da câmera; medir o erro em distâncias e escalas representativas antes de escolher a correção.

## 7 · P1 — Nuvens: movimento, zoom e qualidade

- As nuvens artificiais são a opção padrão e existem modos de nuvens reais, artificiais e desligadas. O usuário considerou as artificiais muito artificiais: estudar estrutura de nuvens naturais, escala, forma, densidade, iluminação e variação regional; preservar desempenho e funcionamento offline.
- Na captura de 2/11/2026, as nuvens volumétricas perdem definição e viram manchas ao aproximar, mesmo com qualidade alta. Aumentar a resolução do ray march conforme o orçamento de qualidade e validar a reconstrução em close, sem estourar o orçamento de GPU nem piorar o caso distante. A resolução do ruído volumétrico também deve ser avaliada se o aumento de amostras por tela não bastar.
- Analisar a gravação de 2/11/2026 ao mover câmera e zoom, por quadros quando disponível: eliminar cortes, lâminas, mudanças de escala aparente e instabilidade das nuvens. Conferir continuidade do volume em vistas do solo, dentro da camada e do espaço; registrar se o vídeo original não estiver acessível.

## 8 · P1 — Base do Booster e fidelidade da V3

**Pesquisa e experimento em 3/11:** as referências anotadas para V3 e Pad 2 são [Flight 12](https://www.spacex.com/launches/starship-flight-12) e [atualizações V3](https://new.spacex.com/updates). A migração que usava o centro mapeado da trincheira da Pad 2 foi retirada da integração principal por regressão nas capturas e preservada na worktree de revisão descrita acima. Origem, elevação, eixo e alcance ainda precisam de confronto com imagens/filmagens identificadas antes de reintegração. A missão principal voltou à configuração anterior; a fidelidade V3/Pad 2 permanece pendente.

### Fechar corretamente a vista para dentro do tanque

O problema relatado era a vista para dentro do tanque através das aberturas entre os bocais. A origem era a ausência da superfície inferior do tanque; o fechamento precisa ficar acima dos motores, preservando os vãos abertos da região de motores da V3.

**Avanço parcial em 3/11:** corrigida a intrusão de linhas, flange e apoios dos atuadores no interior dos bocais da Ship e do Booster. O teste novo reproduziu o defeito antes da correção e passou depois nos dois níveis de malha, com motores central/periférico/a vácuo e vetoração. Build e dois testes visuais aprovados; capturas antes/depois inspecionadas, sem erros WebGPU, com as mesmas contagens de geometria e draws nessa correção de bocais.

**Continuação em 3/11:** acrescentado o domo inferior fechado e curvo no Booster e na montagem completa, na estação de tanque existente. Referência primária inspecionada: Colleen Liedtke/NSF, B19, 17/3/2026. Curvatura e espessura são estimativas documentadas. O perímetro acompanha o casco nos dois LODs e a borda foi chanfrada para evitar superfície coplanar. O teste de fechamento reproduziu o tanque aberto antes da alteração. Também corrigida uma diferença de 48 cm na posição dos anéis entre os LODs. Corpos superiores dos motores, suportes, corte externo final do casco e percursos fiéis das linhas ainda precisam ser reconstruídos em conjunto. Fontes, evidências e limites em [BOOSTER_V3_ENGINE_REVIEW.md](BOOSTER_V3_ENGINE_REVIEW.md).

Validação final do domo/anéis: build aprovada, dois testes visuais aprovados (35,19 s), seis capturas diagnósticas produzidas sem erros WebGPU. Vistas inferior, axial, lateral, com gimbal e da montagem completa inspecionadas. O domo usa o draw de hardware já existente; adiciona 3.200/1.664 triângulos nas malhas próxima/distante. Nenhuma definição de física/orientação de voo/captura foi alterada. A folga entre motores em toda a vetoração continua pendente.

- A superfície deve preencher os vãos estruturais entre motores e ligar-se corretamente ao anel/base do tanque e aos suportes. Deve ter espessura, material, contorno e recortes coerentes com as fotos.
- Manter todos os bocais expostos e livres para vetoração; não transformar a correção numa saia comprida, num disco cobrindo os bocais ou em carenagens individuais sem evidência de que existem na V3.
- Conferir vistas exatamente axiais, oblíquas e laterais, inclusive com motores orientados em extremos da vetoração. Não pode haver interior do tanque aparecendo por frestas indevidas, nem painéis atravessando bocais ou suportes.
- Investigar e corrigir as peças que se cruzam no conjunto de motores: bocais entrando uns nos outros, texturas/superfícies vazando para dentro do bocal, elementos que parecem flutuar e brilho interno branco excessivo. Não esconder a sobreposição só com câmera ou efeitos de chama.

### Posicionamento físico dos motores

- Modelar a matriz real de 33 motores do Booster conforme referências V3, com os grupos interno vetorizável e externo fixo no arranjo e escala corretos.
- Os motores centrais devem projetar-se um pouco mais para baixo que os do anel intermediário; a diferença é pequena, mas visível. Fazer o mesmo ajuste no modelo físico e na representação visual, mantendo pivôs, vetoração, bocais, plumas e diagnósticos alinhados.
- Garantir espaçamento e envoltórias corretos. Na vista inferior, não deixar bocais interpenetrando; na vista lateral, preservar a projeção relativa real e a base estrutural correta.

### Tubulações do Booster e da Ship

Pesquisar os percursos da V3 em fotografias e fontes confiáveis antes de substituir ou adicionar geometrias. Hoje as linhas parecem inconsistentes com o veículo real; em alguns ângulos uma linha entra num apoio, termina no ar ou parece ligada a lugar nenhum.

- Documentar separadamente tubulações visíveis da Ship e do Booster: ponto de origem e destino, lados/azimutes, mudanças de direção, suportes, abraçadeiras, conexões e relação com os motores e com o casco.
- Fazer cada trecho acompanhar suportes e estrutura reais. Terminações precisam chegar a uma conexão visível; nenhuma tubulação deve atravessar um apoio sólido, a parede do tanque, outra peça ou o interior de um bocal.
- Incluir as linhas e estruturas de suporte/riser que realmente aparecem no Booster V3. Verificar em fotos o que o usuário chama de “torres de tubulações”; não criar torres repetitivas ou um emaranhado decorativo por suposição.
- A tubulação lateral da Ship mostrada pelo usuário precisa ser verificada em close: esclarecer visualmente se passa por dentro do suporte ou se apenas parece assim por oclusão/perspectiva e ajustar o percurso à referência real.
- Detalhes menos visíveis podem ser simplificados a distância; não devem mudar localização, conectividade ou escala física.

### Escudo térmico e proteção na região dos motores

- Verificar a cobertura real de tiles da Ship V3, que o usuário apontou como estendendo-se mais longe nesta região. Atualizar limite, extensão ao redor da barriga/base e transições usando fotos V3 da variante correta.
- Verificar também o acabamento da barriga do Booster V3. O usuário suspeita que Booster e Ship não têm aquela proteção/carenagem traseira em torno dos motores. Confirmar em imagens atuais antes de representar qualquer proteção.
- Distinguir claramente tiles do casco, proteção intersticial entre motores e carenagens/skirt individuais. Não colocar carenagens individuais ou uma chapa larga atrás dos bocais por conveniência visual.

## 9 · P1 — Torre, SQD e conexões com o veículo

- Pesquisar a torre e o SQD reais, especialmente a versão usada com Starship V3. Refazer o SQD com o mesmo formato, função e pontos de conexão do equipamento real, sem reinterpretá-lo como um braço genérico.
- Corrigir a tubulação do SQD que atualmente parece terminar no ar. Mostrar conexão física contínua entre a torre, o cabeçote móvel e a interface correspondente do veículo; conferir altura, curvatura, folga e posição durante acoplamento/retração.
- Tornar tubos, mangueiras, suportes e braços coerentes com fotografias: número de linhas, diâmetro relativo, curvaturas e pontos de ancoragem. Evitar que atravessem o tanque, braços ou treliças.
- Resolver a peça no topo da torre que apresenta sobreposição/briga de textura e z-fighting.
- Resolver o pequeno quadrado ao lado da Ship e as peças quadradas/saliências que brigam com a tubulação. Substituir a forma só após pesquisar a peça real; se não houver evidência, remover o detalhe fictício.
- Conferir em vista geral e close, com encaixe conectado e desconectado, sem esconder interseções por ângulo de câmera.

## 10 · P2 — Áudio e sincronização

- Corrigir a mixagem na qual o volume da tank farm encobre os jatos/ventilações da nave. Rever níveis, distância, prioridade e coexistência das fontes; reduzir a tank farm sem silenciar os efeitos que deveriam ser ouvidos.
- Verificar sincronização entre visual e áudio para vents/jatos da Ship e do Booster, ventilação dos tanques, partida/corte dos motores, deluge e efeitos de pouso. Usar estado/tempo físico que já alimenta a animação, evitando atrasos perceptíveis ou eventos sonoros fora de fase.
- Testar com avanço de tempo, pausa, salto para marcos, retorno/replay e câmera em diferentes distâncias. Áudio atrasado pela propagação deve continuar coerente com o evento visual e com o modelo acústico.

## 11 · P2 — Motores ligados, reentrada e dilúvio

- Elevar a qualidade visual dos motores ligados de Ship e Booster: materiais e geometria dos bocais, gimbals, atuadores, vetoração, tubulações e estrutura entre motores. A referência deve ser o hardware V3 real e o estado ligado, não apenas um brilho branco dentro de cada sino.
- Substituir o halo de fogo irreal na queima de pouso por uma pluma coerente com o motor, pressão/altitude, vetoração, solo e fase da queima. Conferir ignição, descida, toque e corte em movimento.
- Melhorar muito os efeitos de reentrada, mantendo relação com velocidade, aquecimento e atitude e evitando brilho que esconda o veículo.
- Refazer a animação de dilúvio: jatos, quebra em gotas, escoamento, vapor, vento e interação com a pluma; avaliar em qualidade máxima e também em qualidade baixa. Corrigir a aparência “tosca” relatada nas capturas.
- Sincronizar as animações de dilúvio e plumas com eventos e sons, sem partículas/água aparecendo fora do estado de fluxo correspondente.

## 12 · P2 — Órbitas e trajetórias

- Durante a missão, ocultar as órbitas de Lua/Terra e outros corpos por padrão; manter uma escolha explícita para exibi-las novamente.
- Na vista da Terra, distinguir claramente trajetória já percorrida, previsão a partir do estado atual e trajetória/órbita desejada. Usar estilos legíveis e diferentes, direção temporal e enquadramento apropriados; não sugerir que uma previsão balística simplificada é a trajetória planejada real.
- Melhorar a aparência das linhas de órbita e evitar sobrecarga visual quando há vários corpos.
- Corrigir a oclusão/composição relatada nas linhas de órbita: nuvens ou a região escura/terminador não podem cobrir indevidamente uma linha que está à frente do planeta. Distinguir isso da oclusão correta do trecho atrás da Terra e verificar em movimento.

## 13 · P3 — Catálogo de objetos em órbita terrestre

- Implementar pesquisa em APIs para satélites artificiais que orbitam a Terra, incluindo ISS e constelações; oferecer no menu pesquisa e seleção explícita de quais objetos/órbitas exibir.
- Estudar e documentar fonte, formato de elementos orbitais, propagação e referenciais antes de implementar. Exibir época/atualização dos dados e tratar indisponibilidade, limites da API, cache e dados antigos.
- Evitar carregar ou desenhar todo o catálogo por padrão; manter seleção, visibilidade, desempenho e legibilidade ao alternar objetos individuais e constelações.

## 14 · P3 — Auditoria final e imagens do README

- Fazer uma auditoria visual completa da missão (preparação, contagem, lançamento, separação, retorno/captura do Booster, órbita, reentrada e pouso). Procurar outros detalhes incorretos, problemas de textura, escala, oclusão, materiais e transições e acrescentar ao backlog com referências.

Só depois dos ajustes e da validação, produzir capturas novas na qualidade máxima e atualizar as imagens antigas do README. Priorizar beleza e composição; não é necessário mostrar cada recurso novo se uma cena que já funciona estiver mais bonita.

- Escolher ângulos e iluminação que mostrem formas legíveis: Terra/atmosfera perto do terminador; torre e SQD conectados; lançamento e dilúvio; separação com os dois veículos; captura do Booster; reentrada da Ship.
- Comparar vários enquadramentos e selecionar os visualmente mais fortes, com veículo/estrutura em escala, horizonte e luz convincentes.
- Usar capturas reais do simulador; atualizar imagens, dimensões, legendas e texto alternativo no README e os créditos correspondentes.

## Validação e critérios de conclusão

- Compilar o alvo real do programa e executar os testes adequados. Reproduzir o erro WebGPU já relatado após compilação (`Index 26112 extends beyond limit 576`) e garantir que a submissão de comandos não aborta. Auditar índices de todos os meshes/batches novos e existentes tocados.
- Capturar referências renderizadas de cada área corrigida nos ângulos necessários. Inspecionar visualmente as imagens antes de declarar que uma interseção, abertura, vazamento de textura ou costura desapareceu.
- Validar qualidade alta/baixa, câmera próxima/distante e relevo ligado/desligado; conferir mudança de LOD, pausa/salto temporal e objetos animados.
- Medir ou comparar o custo de renderização quando uma solução aumenta tesselação, resolução, volumes ou número de peças. Não degradar a fluidez da missão para resolver apenas uma captura estática.
- Concluir cada item com evidência: fonte de referência para fidelidade, teste/build, capturas antes/depois e limitações de detalhes que não são publicamente conhecidos.

## O que o estado atual parece já cobrir

Segundo o README e o código encontrado durante a consolidação, o Albert já inclui a missão Starship, mantém a missão incorporada na distribuição, oferece controles de tempo e saltos a marcos, oculta outras órbitas inicialmente, desenha trajetória na Terra, inicia o painel de interferência fechado à direita e oferece nuvens observadas/artificialmente simuladas/desligadas com as simuladas como padrão. Isso é apenas um lembrete de auditoria: confirmar no programa atual e não refazer esses recursos sem um defeito reproduzível.

Registro histórico de 2/11/2026: a correção inicial estava no worktree e havia uma instância da MX350 para validação. As instâncias de diagnóstico de 3/11 foram encerradas ao término; resultados atuais abaixo.

Estado do trabalho atual em 3/11/2026:

- A integração principal da missão foi restaurada ao commit `7966519` nos headers `starship.hpp`, `ship_return.hpp` e `starship_flight.hpp` após confirmar a perda de capturas com a migração para a Pad 2. O experimento, os testes e o diagnóstico foram preservados em `/unreal/allan/albert-engine-pad2-review`.
- Câmera livre com WASD horizontal, Espaço/Shift vertical e Ctrl para velocidade, slider logarítmico até um ano/s, margem ampliada de seleção de imagens, ajuste de resolução das nuvens e liberação de texturas GPU continuam como alterações locais na árvore principal. Exigem validação visual e de desempenho.
- A comparação isolada do código anterior e a reprodução na árvore principal restaurada confirmaram a mesma captura nominal do Booster mantida por 30 s, com 0,024146 m/s. A Ship também foi capturada no teste de queda aos 137,9 s e permaneceu nos braços por mais 30 s, com 0,034 m/s. A compilação de `AlbertEngine` e `starship_test` passou. CTest da árvore principal restaurada terminou: 47 aprovados, nenhum erro, dois testes longos desativados (305,07 s). Não tratar o teste de queda como validação do retorno orbital completo.
- A fila agora também registra os pedidos que faltavam na consolidação: layout de data/hora, Starbase nos locais, unificação do menu Cena, composição das linhas de órbita, Sol escuro/reflexos sutis e pesquisa/seleção de satélites terrestres/ISS/constelações.
- A fila foi reordenada por relevância em 14 prioridades, preservando os 92 itens que já estavam anotados. O primeiro avanço na nova ordem foi corrigir a retenção de versões antigas de texturas em materiais ocultos; testes aprovados nas GPUs MX350/Iris Xe e em software. A reprodução integrada foi concluída em seguida: oito cargas reais por GPU, sem crash, cache de volta à mesma base; pico NVIDIA de 791 MiB. Permanecem casos de voo/vista auxiliar, redimensionamentos e sessão mais longa.
- Correção geométrica dos bocais realizada em 3/11: peças externas deixaram de invadir o interior nos casos verificados, sem alterar a definição física ou a captura. O domo inferior do tanque do Booster também foi acrescentado nesta continuação, com referências e dimensões estimadas registradas. Corrigida a posição dos anéis ao trocar LOD. Isso não conclui a reconstrução V3; corpos superiores dos motores, suportes, recorte externo, percursos reais e a aproximação que reduz o raio visual dos bocais periféricos continuam explicitamente pendentes em `BOOSTER_V3_ENGINE_REVIEW.md`.
- Capturas nominais, inspeção visual, precisão em Plutão, retorno orbital e os demais itens permanecem sujeitos aos critérios acima. Não promover a migração da Pad 2 com base apenas em compilação ou em cenários de desvio seguro.

- Continuação após a mudança de prioridades pedida pelo usuário: Sol/atmosfera na posição 2 e interface/tempo na 3. Corrigida a composição do disco solar, com comparação de sete cenários; controles de cena reunidos, campos UTC e ajuda da câmera ajustados. Build aprovada; quatro testes direcionados aprovados (97,89 s). Fontes e resultados em `SUN_ATMOSPHERE_REVIEW.md` e `GPU_TEXTURE_LIFETIME.md`.
