# Revisão da base e dos motores V3 — 3/11/2026

## Referências e limites

- [NASA, 31/7/2026 — ensaios do Super Heavy V3](https://www.nasa.gov/directorates/esdmd/artemis-campaign-development-division/human-landing-system-program/nasa-spacex-advance-wind-tunnel-tests-for-starship-rocket/): confirma 33 motores, ausência de saia na região dos motores, de carenagens individuais e de grande escudo de base. Não fornece desenho de fabricação da cúpula inferior ou das linhas de alimentação.
- [SpaceX — apresentação da V3](https://new.spacex.com/updates): referência da variante; a pesquisa indexada recuperou a apresentação, mas a abertura direta da página falhou por timeout nesta revisão.
- Foto fornecida pelo usuário: `/home/allan/Downloads/Inspected image.jpeg`. Mostra a lateral inferior, anéis/linhas circunferenciais, suportes repetidos, corpos dos motores expostos e acabamento escuro. Não revela todo o fechamento inferior nem os percursos internos. A foto recebida não identifica veículo/data; não atribuir automaticamente a todos os exemplares V3.
- Foi localizado o [registro fotográfico de Max Evans/NSF, Booster 19 em configuração de lançamento](https://x.com/_maxq_/status/2056723472697479284). O acesso ao original retornou 403; não usar como prova de dimensões ou detalhes não inspecionados. Conferir ainda os exemplares V3 posteriores ao B19 antes de uma reconstrução completa.
- Referência primária adicional inspecionada: [NSF, fotografia de Colleen Liedtke, 17/3/2026](https://shop.nasaspaceflight.com/products/slightly-used-raptor-3-engines). O B19 está sendo retirado do suporte, com dez dos 33 motores instalados. A vista parcial permite distinguir a superfície fechada acima dos motores, os corpos dos motores, as conexões e a estrutura externa. Os dispositivos nas posições sem motor são de teste; não copiá-los para a configuração de voo. Cópia local para estudo: `build/mission-review/references/b19-nsf-march.webp`; não incorporada aos assets distribuídos.

## Domo inferior acrescentado nesta continuação

O Booster agora tem uma superfície curva e fechada no lado inferior do tanque, com duas faces e borda unidas. Ela termina no raio do casco e usa a estação traseira do tanque já declarada na aparência (69,3 m após o topo do Booster, incluindo o deslocamento da Ship na montagem completa). A mesma estação de 69,3 m aparece na aproximação física do tanque de LOX do exemplo. O domo fica acima dos motores; não fecha os vãos entre os bocais na altura da saída.

A posição longitudinal usa os dados existentes. A curvatura (profundidade de 15% do raio, 0,675 m no veículo de 9 m) e a espessura visual de 8 mm são aproximações explícitas, sem cotas públicas verificadas. A foto confirma a necessidade e a relação espacial da superfície, não essas dimensões. Não foi criado um escudo grande ou uma saia nova.

O corte externo atual do casco foi preservado. A região superior dos motores ainda é simplificada; removê-lo agora expunha esse problema. A modelagem dos corpos dos motores, seus suportes, o recorte final do casco e as tubulações reais continuam como reconstrução conjunta pendente. Não foram acrescentados postes para preencher esse espaço sem correspondência suficiente com as fotos.

O domo participa do batch de hardware já existente, nas duas malhas de distância e na montagem completa. Posições de motores, área de saída, pivôs, massa, contatos, orientação de voo e coordenadas da plataforma não foram alterados.

Na malha de distância, o perímetro conserva os mesmos 64 segmentos do casco; só o interior da cúpula é simplificado. Isso mantém a ligação fechada na borda. A face interna recua 8 mm no raio junto da borda, formando um chanfro em vez de uma faixa coplanar com o casco. A primeira versão sobrepunha essa faixa e foi corrigida após inspecionar a imagem lateral.

Também foi encontrado um erro independente de LOD nos anéis de suporte existentes: a malha próxima colocava-os 24 cm acima da estação, e a distante 24 cm abaixo. O nível distante agora mantém a mesma estação do próximo; a quantidade de anéis não mudou. O teste de limites longitudinais reproduziu o deslocamento antes da correção. Isso não confirma que os anéis aproximados reproduzem o suporte real da V3; corrige sua mudança de posição com o zoom.

## Correção realizada

O construtor em `src/render/vehicle.cpp` usava raios fixos para as linhas, um flange e os apoios dos atuadores. Esses raios eram menores que a parede divergente do sino naquela altura: peças externas apareciam dentro do bocal. O flange a 8% do comprimento era especialmente evidente.

As posições agora respeitam o perfil já usado para desenhar o sino, incluindo o raio do tubo e uma folga. Os apoios móveis dos atuadores ficam do lado externo. Os reforços e os cotovelos também acompanham o raio visual efetivo do bocal. Nada disso adiciona componentes novos nem constitui uma reprodução precisa das linhas reais da V3: corrige interseções da representação aproximada existente.

Não foram alterados locais/pivôs físicos dos motores, empuxo, vetoração, massa, orientação de voo, coordenadas da plataforma ou controle de captura. A correção conserva a quantidade de peças, vértices, triângulos e chamadas de desenho.

## Verificação

As verificações abaixo da correção anterior de bocais permanecem como histórico. Para o domo, os logs e imagens estão em `build/mission-review/booster-v3-dome-before/` e `build/mission-review/booster-v3-dome-after/`:

- O novo teste falhou com `Booster aft tank is open between the engine mounts` antes do fechamento. Sonda as duas faces do fechamento, entre os motores e perto da borda do casco, nos dois LODs. O teste independente de LOD dos anéis também falhou antes de sua correção.
- Capturas específicas: vista axial, inferior oblíqua, lateral em alta/baixa qualidade, 13 motores vetorizados em pose diagnóstica e montagem completa. Comando: `ALBERT_GPU=software ./build/linux/tests/visual_test aft-captures build/mission-review/booster-v3-dome-after`.
- O teste não certifica a fabricação real da cúpula, a resistência da estrutura ou todas as folgas de gimbal; essas dimensões e os suportes ainda exigem refinamento pelas referências.
- Resultado final desta continuação: build de `AlbertEngine`/`visual_test` aprovada; `visual_controls` e `visual_webgpu` aprovados (35,19 s). As seis capturas foram produzidas com llvmpipe, sem erros de validação WebGPU; vistas inferior, axial, lateral, com gimbal e da montagem completa foram inspecionadas. O artefato coplanar lateral da primeira versão não aparece na captura final. A captura com vetoração extrema continua servindo para a revisão pendente de folgas entre os motores; não declara essa revisão concluída.
- O domo acrescenta 3.200 triângulos na malha próxima e 1.664 na distante, sem chamada de desenho adicional. Totais da cena do Booster: 325.484/84.556 triângulos e 32.149.300/6.453.052 bytes de geometria alta/baixa (mesma contagem de 2 draws). São contagens de cena, não VRAM total; os tempos do renderizador de software não comprovam desempenho na MX350. O código da missão física permaneceu idêntico ao estado validado anteriormente.

- O teste novo de interior livre falhou antes da correção e passou depois. Usa a malha de hardware com as poses efetivamente enviadas à GPU: motor ao nível do mar central e periférico, motor a vácuo, as duas malhas de distância e poses neutra/±0,26 rad com componente no segundo eixo. Raios radiais verificam o espaço interno em estações ao longo do sino. É uma regressão de intrusão no bocal, não uma certificação de folgas entre todos os motores.
- Build de `AlbertEngine` e `visual_test` aprovada.
- `visual_controls` e `visual_webgpu`: dois testes aprovados, 23,01 s.
- Capturas comparáveis: `build/mission-review/booster-v3-audit-before/` e `build/mission-review/booster-v3-audit-after/`. Usam as definições reais, motores desligados e luz controlada, com vista inferior e close dos conjuntos da Ship e do Booster. As poses são diagnósticas, sem avanço da missão.
- As vistas inferiores de Ship/Booster foram inspecionadas: os anéis e os fragmentos de tubulação que apareciam nos interiores deixaram de aparecer nesses enquadramentos. As aberturas da região estrutural continuam visíveis e pendentes. O run final usou llvmpipe e registrou zero erros de validação WebGPU.
- Mesmas contagens antes/depois: Booster, 2 draws, 322.284 triângulos e 32.036.540 bytes de geometria na vista inferior; Ship, 6 draws, 22.680 triângulos e 1.219.032 bytes. Esses totais incluem o restante do veículo na cena e não medem VRAM total. Os tempos em software variaram e não comprovam desempenho na MX350.

## Próximas etapas, ainda pendentes

1. Refinar o domo já acrescentado com mais fotos inferiores/laterais identificadas e, se disponíveis, dimensões. Conferir ligações aos suportes e à alimentação dos motores; o fechamento geométrico foi corrigido, a reconstrução externa completa permanece pendente.
2. Substituir a tubulação aproximada por percursos identificados: origem/destino, azimute, conexões, suportes e relação com a cúpula e com os motores. A foto lateral sozinha não permite reconstruir tudo.
3. Rever envelope e raio dos bocais junto dos locais físicos dos motores. O código anterior já reduz visualmente em 10% o raio da boca dos motores periféricos para evitar sobreposição, mantendo a área física original. Essa aproximação permanece e precisa ser resolvida com dimensões/layout pesquisados, com validação da missão caso os dados físicos mudem.
4. Confirmar as pequenas diferenças axiais entre os grupos. As atuais diferenças de 0,12/0,24 m são estimativas da imagem, compartilhadas entre desenho e definição física; não são cotas de fabricação.
5. Validar interseções entre motores, ligações dos atuadores e folgas em toda a faixa de vetoração, incluindo vistas axiais/laterais, chama e transições de LOD. O teste de interior livre não cobre todas essas condições.

O checklist consolidado continua em [visual-improvements-queue.md](visual-improvements-queue.md). A migração experimental da Pad 2 permanece isolada em `/unreal/allan/albert-engine-pad2-review`.
