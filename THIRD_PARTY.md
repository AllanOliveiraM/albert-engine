# Bibliotecas e dados de terceiros

As versões baixadas são fixadas com SHA-256 em `cmake/Dependencies.cmake` e `cmake/Grpc.cmake`.

| Componente | Versão | Fonte / licença |
| --- | --- | --- |
| threepp | `8518a6c22d051775c45dbd0c04a3ba92e92a5366` | https://github.com/markaren/threepp — MIT; contém outras bibliotecas em `src/external`, com suas respectivas licenças |
| GLFW | 3.4, incluída na threepp | https://www.glfw.org — zlib/libpng |
| Dear ImGui | 1.92.6 (`6ded5230d043aa32c755e65c910c2af5002fb9f9`) | https://github.com/ocornut/imgui — MIT |
| nlohmann/json | 3.12.0 | https://github.com/nlohmann/json — MIT |
| ERFA | 2.0.1 | https://github.com/liberfa/erfa — BSD 3 cláusulas, derivada de IAU SOFA |
| libcurl | 8.22.0 | https://curl.se — curl license |
| gRPC | 1.71.0 | https://github.com/grpc/grpc — Apache 2.0 |
| Protobuf | revisão `2d4414f384dc499af113b5991ce3eaa9df6dd931` | https://github.com/protocolbuffers/protobuf — BSD 3 cláusulas |
| Abseil | revisão `4447c7562e3bc702ade25105912dce503f0c4010` | https://github.com/abseil/abseil-cpp — Apache 2.0 |
| RE2 | revisão `0c5616df9c0aaa44c9440d87422012423d91c7d1` | https://github.com/google/re2 — BSD 3 cláusulas |
| c-ares | revisão `6360e96b5cf8e5980c887ce58ef727e53d77243a` | https://github.com/c-ares/c-ares — MIT |
| zlib | revisão `09155eaa2f9270dc4ed1fa13e2b4b2613e6e4851` | https://github.com/madler/zlib — zlib |
| BoringSSL | revisão `c64b8fefbba9a9dadda73138062fc449bdf11e2a`, Windows/gRPC | https://github.com/google/boringssl — ISC/OpenSSL e avisos dos fontes |
| OpenSSL | Sistema Linux | https://openssl.org — licença da versão instalada |
| GCC runtime | Sistema Linux | https://gcc.gnu.org — GPL com GCC Runtime Library Exception |

As licenças dos fontes baixados, inclusive das bibliotecas incorporadas à threepp,
são copiadas para `dist/<plataforma>/licenses`. No Linux,
consulte também os avisos das bibliotecas do sistema redistribuídas em `lib`.

Assets estáticos (origens, hashes e avisos completos em `assets/manifest.json` e `assets/README.md`):

- NASA Earth Observatory / GEBCO: Blue Marble 2004, Black Marble 2016 e elevação terrestre; NASA/SDO/HMI: intensidade do contínuo solar de referência,
  fontes e condições de uso em `assets/README.md`.
- NASA/GSFC/Arizona State University: mosaico lunar LROC WAC 100 m,
  https://data.lroc.im-ldi.com/lroc/view_rdr/WAC_GLOBAL.
- NASA Scientific Visualization Studio / Ernie Wright (USRA) / NASA GSFC LOLA:
  elevação lunar de 16 bits, https://svs.gsfc.nasa.gov/4720/.
- Natural Earth: fronteiras em domínio público,
  https://www.naturalearthdata.com/about/terms-of-use/.
- HYG 4.1, David Nash / Astronomy Nexus: seleção de 5.070 estrelas,
  https://github.com/astronexus/HYG-Database, CC BY-SA 4.0.
  A seleção distribuída continua sob essa licença.
- Inter, © 2020 The Inter Project Authors: fonte da interface,
  https://github.com/rsms/inter, SIL OFL 1.1; licença em `assets/fonts/OFL.txt`
  nos fontes e `licenses/Inter/OFL.txt` na distribuição.
- NASA GIBS: paleta pública de fração de nuvens MODIS, convertida para JSON,
  https://gibs.earthdata.nasa.gov/colormaps/v1.3/MODIS_Cloud_Fraction.xml.
- Planetas e luas (créditos e limites por corpo em `assets/planetary-sources.json`,
  `assets/solar-sources.json` e `assets/moon-sources.json`, também gravados no pacote):
  NASA/JHUAPL/CIW/USGS MESSENGER (Mercúrio); NASA Visualization Technology
  Applications and Development (VTAD), modelos 3D de planetas e luas; NASA GSFC/SVS
  (Júpiter); NASA/JPL/Caltech/USGS Viking (Marte, Fobos e Deimos).
- Formas medidas: DLR, Willner, Shi e Oberst (2014), conversão NAIF (Fobos);
  P. Thomas / ESA SPICE v0.2 (Deimos); Gaskell, R.W. (2020), Gaskell Mimas Shape
  Model V1.0, NASA PDS, https://doi.org/10.26033/22n9-v553 (Mimas).
- NASA/JPL-Caltech/Space Science Institute/Lunar and Planetary Institute, mosaico
  de Paul Schenk: mapa global de Mimas PIA18437 em cores realçadas,
  https://science.nasa.gov/photojournal/color-maps-of-mimas-2014.

Dados dinâmicos:

- NASA/JPL Horizons: https://ssd.jpl.nasa.gov/horizons/ — efemérides e órbitas.
- NASA GIBS / Terra e Aqua MODIS: fração de nuvens diurna/noturna,
  https://gibs.earthdata.nasa.gov/layer-metadata/v1.0/MODIS_Terra_Cloud_Fraction_Day.json,
  https://gibs.earthdata.nasa.gov/layer-metadata/v1.0/MODIS_Terra_Cloud_Fraction_Night.json.
  https://gibs.earthdata.nasa.gov/layer-metadata/v1.0/MODIS_Aqua_Cloud_Fraction_Day.json,
  https://gibs.earthdata.nasa.gov/layer-metadata/v1.0/MODIS_Aqua_Cloud_Fraction_Night.json.
  As camadas visualizam dados públicos MOD06/MYD06 e suas versões NRT;
  reflectância corrigida True Color de Terra/Aqua acrescenta detalhes observados,
  https://gibs.earthdata.nasa.gov/layer-metadata/v1.0/MODIS_Terra_CorrectedReflectance_TrueColor.json,
  https://gibs.earthdata.nasa.gov/layer-metadata/v1.0/MODIS_Aqua_CorrectedReflectance_TrueColor.json.
  A máscara visual derivada pode confundir neve/solo claro com nuvens; os mosaicos são observações,
  não uma fotografia simultânea nem previsão meteorológica.
- NASA/NAIF: coeficientes públicos de orientação IAU_MOON do kernel
  https://naif.jpl.nasa.gov/pub/naif/generic_kernels/pck/pck00011.tpc,
  implementados como aproximação trigonométrica no código.

Toda a biblioteca visual estática acompanha a distribuição em `assets/visuals.pak`.
Fontes, datas, resolução, codificação de elevação e SHA-256 dos blocos estão no
índice interno e em [docs/ASSETS.md](docs/ASSETS.md). Não há downloads de superfícies
em runtime. O contêiner organiza imagens já comprimidas; não é criptografia.
Imagens meteorológicas baixadas ficam em `data/clouds`, ao lado do executável,
ou em `ALBERT_DATA_DIR`, sob o limite de cache configurado.
