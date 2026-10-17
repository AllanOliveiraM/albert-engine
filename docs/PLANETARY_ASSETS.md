# Conteúdo planetário e fontes

Inventário atualizado em 16/10/2026. Mercúrio está incluído com superfície,
relevo, orientação IAU, efemérides e órbita reais. O pacote adicional de
1.693.650.662 bytes foi preparado, verificado e integrado ao arquivo único.
Os oito planetas estão embarcados. A ampliação da qualidade e as luas detalhadas
seguem em andamento; as luas adicionais do catálogo usam esferas simplificadas.

## Primeiro candidato: Mercúrio

Mercúrio permite validar o caminho de um novo planeta sólido sem introduzir
anéis ou um novo modelo de atmosfera. A escolha é técnica; a aparência final
depende da validação do mosaico e do relevo.

| Produto | Uso e limite conhecido | Fonte primária |
| --- | --- | --- |
| MESSENGER MDIS Basemap BDR, 166 m | Mosaico global monocromático de reflectância, 256 pixels/grau; primeira opção de superfície | [USGS](https://astrogeology.usgs.gov/search/map/mercury_messenger_mdis_global_basemap_bdr_166m) |
| MESSENGER MDIS Color Mosaic, 665 m | Candidato complementar; conferir processamento e interpretação das cores antes de usar como aparência natural | [USGS](https://astrogeology.usgs.gov/search/map/mercury_messenger_mdis_global_color_mosaic_665m) |
| Catálogo de produtos MESSENGER | Seleção de modelo de elevação; não inferir altitude de uma imagem de cor | [USGS](https://astrogeology.usgs.gov/search/map/mercury-messenger-global-products) |

## Segundo candidato: Marte

| Produto | Uso e limite conhecido | Fonte primária |
| --- | --- | --- |
| Viking Colorized Global Mosaic, 232 m | Candidato de superfície; produto colorizado, que precisa ser identificado dessa forma | [USGS](https://astrogeology.usgs.gov/search/map/mars_viking_colorized_global_mosaic_232m) |
| Mars Image Texture | Textura de modelos produzida a partir de imagens Viking processadas pelo USGS; referência para comparação | [NASA](https://science.nasa.gov/3d-resources/mars/) |
| Catálogo Viking | Produtos alternativos para comparar cobertura e fidelidade | [USGS](https://astrogeology.usgs.gov/search/map/mars-viking-global-products) |

## Corte de implementação

1. Registrar ID do corpo, fonte/produto, URL, SHA-256, créditos, condições de
   redistribuição, projeção, convenção de longitude/latitude, escala de elevação
   e versão do processamento. As [diretrizes NASA](https://www.nasa.gov/nasa-brand-center/images-and-media/)
   não substituem a conferência dos créditos de cada produto.
2. Inspecionar cabeçalhos, cobertura, no-data e tamanho antes de baixar os
   originais. Medir disco e memória com um recorte de teste; não prometer um
   tamanho final de pacote com base apenas na resolução nominal.
3. Gerar os níveis globais e blocos regionais com a infraestrutura atual de
   assets. Incluir no mesmo `visuals.pak`, com lookup por corpo/camada e fontes
   reproduzíveis. Originais e intermediários permanecem em `asset-packs/`.
4. Cadastrar o corpo pelo registro, buscar efemérides reais e aplicar orientação
   documentada. Validar longitude zero, polos, terminador e escala antes de
   habilitar na seleção de objetos. Não usar rotação decorativa como orientação.
5. Validar carregamento durante movimento, oclusão, cache GPU limitado e
   liberação de recursos ao trocar o foco. Comparar imagens e orçamento de
   memória no Linux e Windows antes de marcar M3 como entregue.

Vênus, gigantes gasosos, luas e anéis exigem inventários específicos. Permanecem
no escopo do roadmap, sem reutilizar mapas de outro corpo como substitutos.

## Mercúrio: decisões do primeiro corte

- Fonte de superfície: cópia ISIS do mosaico MDIS BDR no arquivo oficial NASA/PDS;
  grade 92160 × 46080, aproximadamente 166 m/pixel. Monocromática, normalizada
  fotometricamente; não é uma fotografia em cores naturais nem albedo calibrado
  para fotometria absoluta.
- Relevo: `Mercury_Messenger_USGS_DEM_Global_665m.cub`, arquivo original de 2016,
  23040 × 11520. Não confundir com a revisão V02 posterior do catálogo USGS.
  Amostras SignedWord × 0,5 m, datum esférico de 2439400 m. Longitudes 0–360 são
  rearranjadas para −180–180 sem interpolar posições. PNG de 16 bits conserva
  a quantização original; regiões sem altitude válida usam apenas o datum.
- Cinco níveis por camada; JPEG 94 para a superfície e PNG sem perdas para
  altitude. Original, SHA-256 e processamento constam do plano e do pacote.
- Orientação: modelo IAU 2015 de `pck00011`, com libração física. Comparado com
  CSPICE `pxform(IAU_MERCURY,J2000)` em quatro épocas, sem depender de SPICE em
  runtime. SHA-256 do kernel usado nas fixtures:
  `3dff7b1dbeceaa01f25467767d3fa25816051c85d162d1edf04acb310ee28bb1`.
- Iluminação própria do sólido considera a direção/distância real do Sol,
  espalhamento difuso de uma superfície rugosa e tamanho angular solar no
  terminador. Não há iluminação auxiliar ligada por padrão. Microestrutura e
  sombras presentes no mosaico de aquisição continuam sendo uma limitação real
  da textura; não são um modelo completo de transporte de luz sobre cada cratera.
- Estado geocêntrico e órbita heliocêntrica vêm do Horizons, seguindo a mesma
  atualização automática dos corpos atuais. Novos corpos sólidos entram pelo
  descritor e por camadas nomeadas; Terra, Lua e Sol preservam seus modelos próprios.
- Detalhes regionais continuam sendo preparados no worker, enviados à GPU em
  parcelas e revelados gradualmente. A região detalhada é liberada após deixar
  o corpo, mantendo a imagem global pequena disponível para nova navegação.

Validação: 19 testes CTest Linux, orientação contra CSPICE em quatro épocas,
comparação de seis coordenadas do DEM original com os tiles finais, transporte
gRPC real e importação pública de Marte. Build MinGW concluído; catálogo, provedores, gráficos e execução completa
também passaram pelo Wine.
A conferência integrada Linux abriu Mercúrio com seus assets reais, aproximou a
câmera, importou Marte pelo catálogo, orbitou manualmente e removeu o alvo.
O ambiente gráfico usa llvmpipe; desempenho em GPU física e Windows nativo ainda
exigem conferência específica. Nenhuma distribuição foi gerada.

## Oito planetas — expansão inicial

`assets/solar-sources.json` fixa URLs, SHA-256, créditos e limites dos seis novos
planetas. Imagens NASA VTAD são extraídas dos GLBs oficiais. O atlas de Saturno
reamostra as UVs reais da malha por triângulo, em faixas de 64 linhas.
Júpiter usa o [mapa visual de 6000×3000 do NASA GSFC/SVS](https://svs.gsfc.nasa.gov/13007/),
publicado em 2018, separado das imagens de campo magnético da mesma página.
As originais permanecem em `asset-packs/solar/`; o arquivo otimizado adicional
pesa 275.610.368 bytes. O build reúne os três arquivos lógicos em um único
`visuals.pak` de 4.824.701.012 bytes (~4,82 GB).

Resoluções: Vênus 1440×720; Marte 32768×16384 (Viking colorizado via NASA Trek); Júpiter 6000×3000; Saturno 4096×2048 após reprojeção;
Urano/Netuno 1024×512. Blocos com gutter, mipmaps e recortes seguem o caminho
existente de upload gradual. Não há ganho de detalhe além da fonte, nem DEM
nesses seis corpos: o relevo fica zero explicitamente, sem derivá-lo do brilho.
Vênus representa topo atmosférico, não superfície por radar. Os gigantes usam
texturas de visualização estáticas, sem afirmação de clima atual ou albedo
calibrado. A longitude das feições depende do registro visual original e ainda
não possui validação cartográfica independente; o eixo/rotação física têm.

Raios equatoriais/polares e orientação vêm de NAIF pck00011, incluindo termos
periódicos de Marte/Júpiter/Netuno. `scripts/import-planet-frames.py` gera os
coeficientes em C++; fixtures independentes CSPICE cobrem três épocas por planeta.
O achatamento é geométrico. A iluminação mantém a variação solar com a distância;
a exposição fotográfica adapta-se suavemente ao planeta focado.

Anéis de Saturno: perfil RGBA do mesmo recurso NASA, raio interno 1,23 e externo
2,3309 vezes o raio equatorial, plano equatorial, transparência e sombra do planeta
sobre os anéis. É um primeiro modelo visual: sombra dos anéis no planeta e dispersão
física de partículas continuam pendentes.

Anéis de Júpiter, Urano e Netuno (`src/render/ring_systems.*`): raios, larguras e
profundidades ópticas normais das tabelas do PDS Ring-Moon Systems Node (de Pater et
al. 2018; Nicholson et al. 2018; Showalter e Lissauer 2006; Porco et al. 1995). O
perfil é uma textura de 8192 faixas com a profundidade óptica média de cada faixa,
separada em partículas macroscópicas e poeira, para que anéis mais estreitos que uma
faixa (Urano: 1,5–9 km) mantenham a largura equivalente; mipmaps evitam serrilhado de
longe. A luz segue uma camada fina em espalhamento simples, vista do lado iluminado ou
do lado oposto: partículas muito escuras (albedo geométrico 0,03, fase de esfera de
Lambert) e poeira de albedo 0,5 com Henyey-Greenstein g = 0,7, em cinza neutro, com a
sombra do planeta. No brilho físico, esses anéis são quase invisíveis em luz visível,
como na realidade; a opção **Realçar anéis tênues**, no painel do planeta, estica o
brilho de cada sistema até o trecho mais brilhante chegar a I/F 0,25, como uma longa
exposição, preservando as proporções e a geometria (a poeira fica muito mais brilhante
contra o Sol). Não são desenhados: arcos de Adams, excentricidades e inclinações (o ε
de Urano varia ±400 km), Arago e a cor real das partículas.

Para reproduzir (Python 3 com Pillow >= 9.1 e NumPy >= 1.24; também incluídos
em `scripts/Dockerfile.assets`):

```sh
python3 scripts/prepare-solar-assets.py
# Com as fontes já baixadas e verificadas:
python3 scripts/prepare-solar-assets.py --offline
python3 scripts/import-planet-frames.py --source asset-packs/solar/sources/pck00011.tpc
```

O script publica `assets/imagery/solar`, atualiza manifests e conserva os arquivos
anteriores intactos. `asset-packs/` é ignorada pelo Git. O pacote novo é versionado
com Git LFS. Não é necessário regenerar distribuições.

O carregador escolhe o menor nível de origem que preenche a resolução pedida
e reduz a imagem ao orçamento final; mapas de tamanho não potência de dois
mantêm esse detalhe. As texturas regionais continuam limitadas a 4096 por eixo,
sem ampliar artificialmente dados menores. A leitura mantém o arquivo aberto:
substituições atômicas da biblioteca preservam sessões já abertas. Na montagem
Linux, os blocos copiados são liberados do cache de arquivos em lotes de 64 MiB.

Validação da preparação: pico de aproximadamente 257 MiB no pacote solar;
a reprojeção em faixas preservou os pixels da conversão anterior. Regressões
cobrem blocos parciais, polos, antimeridiano, perfis não geográficos, integridade,
leitura concorrente e troca atômica da biblioteca.

O mapa de Marte embarcado foi preparado a partir do mosaico Viking do
NASA Trek. Para baixar ou retomar o snapshot em blocos de 256×256, sem abrir
o planeta inteiro na RAM (Python 3 e Pillow):

```sh
python3 scripts/download-mars-tiles.py --level 6 --workers 2
```

O nível 6 corresponde à grade 32768×16384, com 8192 blocos. O script valida
dimensões/JPEG dos arquivos existentes, registra seus checksums, repete falhas e só publica
um manifest final quando o nível escolhido estiver completo. Fila e número
de trabalhadores são limitados. O download fica em
`asset-packs/solar/sources/mars-trek/`, fora do Git; o snapshot completo de 373.675.690 bytes já foi processado
e publicado no pacote solar. O mapa atual não é
alterado por esse comando. A maior grade não implica a mesma resolução efetiva
em todas as regiões nem informação além das observações Viking.

Depois que o download inteiro terminar, para validar e publicar esse snapshot:

```sh
python3 scripts/prepare-solar-assets.py --offline \
  --mars-snapshot asset-packs/solar/sources/mars-trek/manifest.json
```

Essa etapa exige GDAL >= 3.6, além de Pillow e NumPy. O `Dockerfile.assets` inclui
essas dependências; exemplo a partir da raiz do projeto:

```sh
docker build -f scripts/Dockerfile.assets -t albert-assets .
docker run --rm --user "$(id -u):$(id -g)" -v "$PWD:/work" \
  --entrypoint python3 albert-assets scripts/prepare-solar-assets.py --offline \
  --mars-snapshot asset-packs/solar/sources/mars-trek/manifest.json
```

O processo verifica todos os blocos antes de publicar, registra o hash do snapshot
em `solar-sources.json` e inclui sua proveniência no pacote. Usa um mosaico virtual,
cache GDAL de 128 MiB e um trabalhador; não monta uma imagem global inteira na RAM.
Um download parcial não substitui o mapa atual. O novo mosaico de 32K está embarcado, sem DEM de Marte. A resolução nominal da grade WMTS é inferior à fonte Viking original de 232 m; não deve ser apresentada como 232 m/pixel.

Teste do gerador com as mesmas dependências:

```sh
docker run --rm -v "$PWD:/work" --entrypoint python3 albert-assets \
  tests/solar_assets_fixture.py
```

Fontes: [NASA Trek/Mars](https://trek.nasa.gov/tiles/apidoc/trekAPI.html?body=mars),
[NASA 3D Resources](https://science.nasa.gov/3d-resources/),
[Mars/Viking](https://science.nasa.gov/3d-resources/mars/),
[NAIF pck00011](https://naif.jpl.nasa.gov/pub/naif/generic_kernels/pck/pck00011.tpc).

Terra e Lua conservam suas grades de 86400×43200 e 108000×54000. O zoom
mínimo agora permite aproximadamente 64 km de altitude na Terra e 20 km na Lua;
os recortes com DEM podem usar até 384×384 segmentos, preparados no worker.
A resolução da fonte continua sendo o limite: mais zoom não cria detalhes novos.

A malha regional interpola bilinearmente o DEM ao criar vértices; aumentar a
subdivisão não deve criar degraus de amostragem e faixas nas normais. A Lua
usa exposição fotográfica própria (2,1), sem alterar a iluminação auxiliar nem
a direção solar física.
