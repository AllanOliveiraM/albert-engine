# Backlog consolidado: missão Starship, V3 e visuais

Este documento reúne os pedidos ainda relevantes da conversa e serve como brief para uma execução futura. **Não significa que os itens abaixo já foram implementados.** Antes de mudar código, audite a árvore atual e confira cada item: várias solicitações de interface e missão já aparecem como implementadas no README e podem ter sido concluídas durante o trabalho anterior.

## Direção obrigatória

- Pesquisar e estudar a configuração **Starship V3** antes de modelar ou alterar qualquer componente. Identificar a variante e a data das referências; não misturar V1/V2, protótipos, renders conceituais e V3.
- Tratar a foto de referência do lado inferior do Booster enviada pelo usuário nesta conversa como referência visual direta. Ela mostra a relação entre saia do tanque, estrutura anular, proteção/interstícios e conjunto de motores. Reavaliar as referências públicas atuais antes de assumir que algum detalhe dessa foto permanece igual em todas as V3.
- Dar preferência a fontes primárias da SpaceX e fotografias nítidas da configuração correta. A página oficial de apresentação da V3 é um ponto de partida: [SpaceX — Starship V3](https://new.spacex.com/updates). Fotos de campo só valem após conferir veículo, variante, data e ângulo.
- Manter um registro curto por detalhe: referência, componente, localização, orientação, dimensões/proporções conhecidas ou estimadas e grau de confiança. Se uma parte não estiver visível ou documentada, não apresentá-la como fato; evitar acrescentar peças plausíveis, mas inventadas.
- As coordenadas físicas, eixos, pivôs, contatos e relações entre peças precisam acompanhar a geometria visível real. Não corrigir a imagem deslocando apenas a malha se isso deixar coordenadas físicas incompatíveis.
- Preservar desempenho, LOD e estabilidade WebGPU. Comparar qualidade alta/baixa, próximo/longe e diferentes ângulos. Corrigir qualquer triângulo cujo índice ultrapasse a contagem de vértices.

## P0 — Base do Booster e fidelidade da V3

### Fechar corretamente a vista para dentro do tanque

A captura axial inferior mais recente mostra grandes aberturas avermelhadas entre os bocais, através das quais parece possível olhar para dentro do tanque. Isso está errado visualmente. Investigar a origem geométrica do fundo aberto e criar a superfície estrutural intersticial necessária para ocultar o interior, seguindo a configuração V3.

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

## P0 — Terreno detalhado integrado ao entorno

A última captura mostra uma área de terreno detalhado pequena, quadrada/retangular e aparentemente destacada sobre a paisagem desfocada, com uma borda azul escura. Ela precisa parecer continuação natural do terreno ao redor, e não um tile pousado sobre o globo.

- Descobrir qual camada produz a borda (imagem regional, malha de relevo, máscara, costa/água, origem local ou sobreposição com o terreno global) e corrigir a causa, não mascarar o quadro com uma peça decorativa.
- Unificar datum, altitude, geóide, escala, coordenadas e normais do relevo local com o terreno global. Evitar degraus, flutuação, frestas, interseção dupla, diferença de cor/iluminação e faixa azul que não corresponda à costa real.
- Fazer a transição visual de textura e resolução desaparecer gradualmente sem borrar o terreno próximo. Manter a costa e as lagoas com forma e cor coerentes com o entorno.
- Corrigir os casos relatados de chão borrado mesmo em alta qualidade, briga/pisca de terreno ao ativar relevo detalhado e fragmentação do terreno em placas quando o relevo detalhado está desligado. A troca de LOD/qualidade não pode fazer o chão piscar ou abrir buracos.
- Reavaliar custo do relevo detalhado: alta nitidez perto da câmera, redução de detalhe distante com transição estável e sem custo desnecessário. Mostrar erro/LOD coerente em vista orbital, aproximação, plano rasante e solo.
- Validar com a Ship/Booster no quadro, para comprovar que o terreno e o veículo partilham escala, posição e horizonte.

## P1 — Torre, SQD e conexões com o veículo

- Pesquisar a torre e o SQD reais, especialmente a versão usada com Starship V3. Refazer o SQD com o mesmo formato, função e pontos de conexão do equipamento real, sem reinterpretá-lo como um braço genérico.
- Corrigir a tubulação do SQD que atualmente parece terminar no ar. Mostrar conexão física contínua entre a torre, o cabeçote móvel e a interface correspondente do veículo; conferir altura, curvatura, folga e posição durante acoplamento/retração.
- Tornar tubos, mangueiras, suportes e braços coerentes com fotografias: número de linhas, diâmetro relativo, curvaturas e pontos de ancoragem. Evitar que atravessem o tanque, braços ou treliças.
- Resolver a peça no topo da torre que apresenta sobreposição/briga de textura e z-fighting.
- Resolver o pequeno quadrado ao lado da Ship e as peças quadradas/saliências que brigam com a tubulação. Substituir a forma só após pesquisar a peça real; se não houver evidência, remover o detalhe fictício.
- Conferir em vista geral e close, com encaixe conectado e desconectado, sem esconder interseções por ângulo de câmera.

## P1 — Áudio, sincronização e câmera

- Corrigir a mixagem na qual o volume da tank farm encobre os jatos/ventilações da nave. Rever níveis, distância, prioridade e coexistência das fontes; reduzir a tank farm sem silenciar os efeitos que deveriam ser ouvidos.
- Verificar sincronização entre visual e áudio para vents/jatos da Ship e do Booster, ventilação dos tanques, partida/corte dos motores, deluge e efeitos de pouso. Usar estado/tempo físico que já alimenta a animação, evitando atrasos perceptíveis ou eventos sonoros fora de fase.
- Testar com avanço de tempo, pausa, salto para marcos, retorno/replay e câmera em diferentes distâncias. Áudio atrasado pela propagação deve continuar coerente com o evento visual e com o modelo acústico.
- A câmera livre relatada fugindo com a rotação da Terra precisa permanecer ancorada ao referencial escolhido. Verificar movimentos prolongados e troca de foco sem deriva causada pela rotação terrestre.

## P1 — Interface de missão e interferências: auditar o estado atual

O README já descreve uma missão Starship incluída no Albert, controles no horário para velocidade/saltos a marcos, órbitas dos demais corpos ocultadas por padrão e vista de trajetória na Terra. Também descreve o painel de interferência à direita e fechado por padrão. Auditar esses fluxos antes de mexer; corrigir somente falhas ainda reproduzíveis.

- Confirmar que apenas a missão Starship está no menu de missões incluídas e que a missão é compilada/distribuída com o programa sem passos manuais de RPC.
- Confirmar o seletor de tempo da missão: pausar/retomar, velocidade e destinos de salto claros, integrados ao menu de tempo, sem botões de pular reentrada espalhados ou duplicados.
- Confirmar que os saltos oferecem os marcos pertinentes (lançamento, separação, retorno/captura do Booster, reentrada e pouso) e deixam explícito o destino e o efeito no tempo.
- Confirmar que o painel de interferências está compacto, encaixado à direita, fechado inicialmente e sem o campo de texto redundante mencionado pelo usuário. Testar abertura, fechamento e layout em diferentes tamanhos de janela.
- Durante a missão, ocultar as órbitas de Lua/Terra e outros corpos por padrão; manter uma escolha explícita para exibi-las novamente.
- Na vista da Terra, distinguir claramente trajetória já percorrida, previsão a partir do estado atual e trajetória/órbita desejada. Usar estilos legíveis e diferentes, direção temporal e enquadramento apropriados; não sugerir que uma previsão balística simplificada é a trajetória planejada real.
- Melhorar a aparência das linhas de órbita e evitar sobrecarga visual quando há vários corpos.

## P2 — Terra, nuvens, motores e efeitos

- Continuar a melhorar a luz solar na Terra e a interação da luz com atmosfera, horizonte e nuvens, conferindo dia, terminador e crepúsculo.
- As nuvens artificiais são a opção padrão e existem modos de nuvens reais, artificiais e desligadas. O usuário considerou as artificiais muito artificiais: estudar estrutura de nuvens naturais, escala, forma, densidade, iluminação e variação regional; preservar desempenho e funcionamento offline.
- Elevar a qualidade visual dos motores ligados de Ship e Booster: materiais e geometria dos bocais, gimbals, atuadores, vetoração, tubulações e estrutura entre motores. A referência deve ser o hardware V3 real e o estado ligado, não apenas um brilho branco dentro de cada sino.
- Substituir o halo de fogo irreal na queima de pouso por uma pluma coerente com o motor, pressão/altitude, vetoração, solo e fase da queima. Conferir ignição, descida, toque e corte em movimento.
- Melhorar muito os efeitos de reentrada, mantendo relação com velocidade, aquecimento e atitude e evitando brilho que esconda o veículo.
- Refazer a animação de dilúvio: jatos, quebra em gotas, escoamento, vapor, vento e interação com a pluma; avaliar em qualidade máxima e também em qualidade baixa. Corrigir a aparência “tosca” relatada nas capturas.
- Sincronizar as animações de dilúvio e plumas com eventos e sons, sem partículas/água aparecendo fora do estado de fluxo correspondente.
- Fazer uma auditoria visual completa da missão (preparação, contagem, lançamento, separação, retorno/captura do Booster, órbita, reentrada e pouso). Procurar outros detalhes incorretos, problemas de textura, escala, oclusão, materiais e transições e acrescentar ao backlog com referências.

## P3 — Imagens do README

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

Este backlog consolida solicitações; ele não registra correções feitas nesta tarefa.
