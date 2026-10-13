# Assets do Albert Engine

As imagens otimizadas ficam em um único **`visuals.pak`**, incluído em cada
pasta de distribuição. O aplicativo lê blocos diretamente desse contêiner,
conforme o zoom, sem extrair imagens nem baixar superfícies. O pacote contém
os níveis globais e regionais, cidades, relevo terrestre/lunar e a imagem solar.
Suas três partes são versionadas em `imagery/` com Git LFS e reunidas no build.
O gerador preserva originais e blocos de trabalho em `asset-packs/`, fora do Git;
esses intermediários não entram na distribuição. Veja [preparação e formato](../docs/ASSETS.md).

`highres-sources.json` declara URLs, datas e créditos da biblioteca. O índice
interno do pacote guarda dimensões, níveis, codificação do relevo, bytes e SHA-256.
As fontes são NASA Earth Observatory (Blue Marble 2004, Black Marble 2016),
NASA/GEBCO08, NASA/GSFC/ASU/LROC WAC, NASA/LOLA e NASA/SDO/HMI.
São referências estáticas, com os limites e resoluções descritos na documentação.
O mosaico lunar conserva iluminação da aquisição; o relevo vem de DEM separado.
A imagem solar é instrumental, representa um hemisfério observado e não mostra
atividade atual. Sem observações meteorológicas válidas, não são inventadas nuvens.

Além das partes do pacote, este diretório mantém dados auxiliares versionados. `manifest.json`
declara seus hashes, verificados pelo CMake antes de compilar. Caminhos são
resolvidos a partir do executável; `ALBERT_ASSET_DIR` e `ALBERT_PACK_PATH`
permitem selecionar diretório e pacote explicitamente no desenvolvimento.

- `countries.geojson`: snapshot Natural Earth, domínio público,
  https://github.com/nvkelso/natural-earth-vector/blob/master/geojson/ne_10m_admin_0_countries.geojson.
  Fronteiras são uma referência cartográfica desta edição.
- `stars.csv`: seleção de 5.070 registros HYG 4.1 com magnitude visual ≤ 6,
  excluindo o Sol. © David Nash / Astronomy Nexus,
  https://github.com/astronexus/HYG-Database, sob
  [CC BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/).
  Esta seleção/adaptação continua sob CC BY-SA 4.0. Coordenadas equatoriais em
  parsecs e velocidades em parsecs/ano, época/equinócio J2000.0, preservadas do
  original. `scripts/import-stars.py` reproduz a seleção a partir do CSV original.
- `cloud-colormap.json`: adaptação da paleta científica pública
  https://gibs.earthdata.nasa.gov/colormaps/v1.3/MODIS_Cloud_Fraction.xml
  em tuplas `[fração_percentual, R, G, B]`. Decodifica a fração observada;
  cores desconhecidas e pixels sem dados permanecem sem cobertura.
- `fonts/Inter.ttf`: Inter, © 2020 The Inter Project Authors,
  https://github.com/rsms/inter, SIL Open Font License 1.1.
  Licença completa em `fonts/OFL.txt`. Distribuído sem alterações.

Efemérides Horizons e imagens MODIS são atualizadas pela internet. O cache
meteorológico fica em `data/clouds`, ao lado do executável, ou no caminho definido
por `ALBERT_DATA_DIR`. A limpeza e o limite do cache nunca alteram `visuals.pak`.
