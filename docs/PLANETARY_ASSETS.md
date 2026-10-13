# Próximo conteúdo embarcado

Inventário consultado em 13/10/2026. M3 está em andamento: registro adicional,
orientação IAU, consulta Horizons, órbita, render de sólidos e pipeline de assets
estão implementados. O DEM original foi baixado e processado; a superfície de
92160 × 46080 pixels ainda está sendo baixada. Mercúrio só será habilitado quando
seu pacote completo estiver incluído. Marte e os demais corpos seguem pendentes.

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

Os 16 testes CTest Linux, renderização com fixtures, build MinGW e testes de
orientação, pacote e gráficos pelo Wine passaram. A conferência com a superfície
real completa e o tamanho final do pacote ainda dependem da conclusão do download.
