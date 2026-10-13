# Próximo conteúdo embarcado

Inventário inicial consultado em 13/10/2026. Nenhum destes arquivos foi incluído
na biblioteca nem representa um planeta já implementado.

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
