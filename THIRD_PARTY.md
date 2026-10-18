# Bibliotecas e dados de terceiros

As versões baixadas são fixadas com SHA-256 em `cmake/Dependencies.cmake` e `cmake/Grpc.cmake`.

| Componente | Versão | Fonte / licença |
| --- | --- | --- |
| threepp | `8518a6c22d051775c45dbd0c04a3ba92e92a5366` | https://github.com/markaren/threepp — MIT; contém outras bibliotecas em `src/external`, com suas respectivas licenças |
| GLFW | 3.4, incluída na threepp | https://www.glfw.org — zlib/libpng |
| Dear ImGui | 1.92.6 (`6ded5230d043aa32c755e65c910c2af5002fb9f9`) | https://github.com/ocornut/imgui — MIT |
| nlohmann/json | 3.12.0 | https://github.com/nlohmann/json — MIT |
| Backend WebGPU do Dear ImGui | 1.92.6, adaptado à wgpu-native v29 em `third_party/imgui-wgpu/` | MIT, como o Dear ImGui |
| wgpu-native | v29.0.1.1, release estático | https://github.com/gfx-rs/wgpu-native — MIT ou Apache 2.0 (`third_party/wgpu-native/`); embute crates Rust do wgpu/naga sob licenças MIT/Apache — inventário completo antes de qualquer distribuição |
| Precomputed Atmospheric Scattering (Bruneton) | implementação de referência de 2017, portada para WGSL em `src/gpu/atmosphere.cpp` | https://github.com/ebruneton/precomputed_atmospheric_scattering — BSD 3 cláusulas (`third_party/bruneton/LICENSE`) |
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
- Lowell Observatory / D. Schleicher (2010): função de fase de poeira composta
  Halley-Marcus, tabela em `src/core/comets.cpp`,
  https://asteroid.lowell.edu/comet/dustphase.html (Schleicher et al. 1998; Marcus 2007).
- NASA Engineering and Safety Center: check cases 6-DOF (NASA/TM-2015-218675),
  https://nescacademy.nasa.gov/flightsim/2015; valores de referência em
  `tests/nesc_fixture.inc` e modelos DAVE-ML da esfera e do foguete de dois estágios
  convertidos em `src/physics/vehicles.cpp`. Obra do governo dos EUA.
- National Geospatial-Intelligence Agency (NGA): coeficientes do modelo gravitacional
  EGM2008 tide-free até o grau 70 em `src/physics/egm2008.inc`, extraídos de
  `EGM2008_to2190_TideFree` (https://earth-info.nga.mil). Distribuição sem restrições.
- NASA Planetary Data System, Geosciences Node: campos gravitacionais GRAIL GRGM1200A
  (Lua, Lemoine et al. 2014; Goossens et al. 2016) e MRO GMM-3 (Marte, Genova et al. 2016),
  tabelas SHADR lidas em uso por faixas de bytes até o grau pedido
  (https://pds-geosciences.wustl.edu); excertos até o grau 8 em `tests/*_gravity_degree8.tab`.
  Dados do governo dos EUA.
- NASA PDS Geosciences Node: relevo LRO LOLA `LDEM_128` (Lua, GSFC) e MGS MOLA MEGDR 128 px/°
  (raios de Marte), lidos em uso por faixas de bytes. Dados do governo dos EUA.
- NASA PDS Atmospheres Node: perfis de entrada das Viking 1 e 2 (Seiff e Kirk 1977), níveis de
  densidade em `tests/central_bodies_test.cpp`.
- NAIF/JPL: elementos rotacionais IAU da Lua e de Marte como em `pck00011.tpc` e a rotação
  entre os eixos principais DE440 e o referencial médio da Terra de `moon_de440_250416.tf`
  (https://naif.jpl.nasa.gov), transcritos em `src/physics/earth.cpp`.
- NASA Glenn Research Center: ajuste da atmosfera de Marte a perfis do Mars Global Surveyor
  (https://www.grc.nasa.gov/www/k-12/airplane/atmosmrm.html), em `src/physics/atmosphere.cpp`.
- NOAA/NCEP: previsões e análises GFS 0,25° baixadas em uso pelo filtro NOMADS
  (https://nomads.ncep.noaa.gov) e índices do NOAA Open Data Dissemination na AWS
  (https://registry.opendata.aws/noaa-gfs-bdp-pds/); recorte da análise de 2026-10-17
  12 UTC e da previsão de 18 UTC +6 h em `tests/gfs_tampa_*.grb2`. Dados do governo dos EUA.
- University of Wyoming, Department of Atmospheric Science: radiossondagens de Tampa
  (72210) de 2026-10-17 12 UTC e 2026-10-18 00 UTC, níveis padrão em `tests/weather_test.cpp`,
  https://weather.uwyo.edu.
- NRLMSISE-00 (Picone, Hedin e Drob, NRL; obra do governo dos EUA): porte em C++ em
  `src/physics/msis.cpp` e coeficientes em `src/physics/msis_data.inc`, a partir da classe
  `NRLMSISE00` do Orekit, © CS GROUP, Apache License 2.0
  (https://www.orekit.org, https://www.apache.org/licenses/LICENSE-2.0); alterações: só a
  versão em `double`, estrutura condensada, interface própria. Licença e NOTICE em
  `third_party/orekit/` e `licenses/Orekit` na distribuição.
- CelesTrak (T. S. Kelso): índices de clima espacial `SW-Last5Years.csv`/`SW-All.csv`
  baixados em uso (https://celestrak.org/SpaceData/), com dados de NOAA/SWPC, GFZ Potsdam e
  NRCan; recorte de 2026-10-12 a 22 em `tests/weather_test.cpp`.
- Copernicus DEM GLO-30 (relevo de 30 m baixado em uso por faixas de bytes do registro aberto
  na AWS, https://registry.opendata.aws/copernicus-dem/): "produced using Copernicus WorldDEM-30
  © DLR e.V. 2010-2014 and © Airbus Defence and Space GmbH 2014-2018 provided under COPERNICUS
  by the European Union and ESA; all rights reserved". The organisations in charge of the
  Copernicus programme by law or by delegation do not incur any liability for any use of the
  Copernicus WorldDEM-30.
- NASA Global Imagery Browse Services (GIBS) / NASA-USGS WELD (Web-Enabled Landsat Data):
  composição anual Landsat de 2000 a ~31 m (`Landsat_WELD_CorrectedReflectance_TrueColor_Global_Annual`),
  baixada em uso para o detalhe dos sítios de lançamento, https://gibs.earthdata.nasa.gov.
- NASA GIBS: paleta pública de fração de nuvens MODIS, convertida para JSON,
  https://gibs.earthdata.nasa.gov/colormaps/v1.3/MODIS_Cloud_Fraction.xml.
- Planetas e luas (créditos e limites por corpo em `assets/planetary-sources.json`,
  `assets/solar-sources.json` e `assets/moon-sources.json`, também gravados no pacote):
  NASA/JHUAPL/CIW/USGS MESSENGER (Mercúrio); NASA Visualization Technology
  Applications and Development (VTAD), modelos 3D de planetas e luas; NASA GSFC/SVS
  (Júpiter); NASA/JPL/Caltech/USGS Viking (Marte, Fobos e Deimos).
- Formas medidas: DLR, Willner, Shi e Oberst (2014), conversão NAIF (Fobos);
  P. Thomas / ESA SPICE v0.2 (Deimos); Gaskell, R.W. (2020), Gaskell Mimas Shape
  Model V1.0, NASA PDS, https://doi.org/10.26033/22n9-v553 (Mimas); Gaskell, R.W.
  (2013), Gaskell Phoebe Shape Model V1.0, NASA PDS,
  urn:nasa:pds:gaskell.phoebe.shape-model::1.0 (Febe); Thomas, P., Joseph, J. e
  Ansty, T. (2018), Saturn Small Moon Shape Models V1.0, NASA PDS,
  https://doi.org/10.26033/ewy3-jy61 (Hipérion); Stooke, P. (2025), Stooke small
  bodies shape models V1.0, NASA PDS, https://doi.org/10.26033/yt84-5y91 (Proteu);
  ESA/Rosetta/MPS for OSIRIS Team (LAM-PSI), DSK `ROS_CG_K195_OSPCLPS_N_V1.BDS` do
  ESA SPICE Service (67P); o mesmo pacote de Stooke, com apontamento de A. Abergel
  (Halley).
- NASA/JPL-Caltech/Space Science Institute/Lunar and Planetary Institute, mosaico
  de Paul Schenk: mapa global de Mimas PIA18437 em cores realçadas,
  https://science.nasa.gov/photojournal/color-maps-of-mimas-2014.
- NASA/JPL-Caltech/University of Arizona (Cassini ISS): mosaico global de Titã em
  938 nm PIA22770, https://photojournal.jpl.nasa.gov/catalog/PIA22770; cor da névoa
  medida em NASA/JPL/Space Science Institute PIA06230.
- NASA/JPL/Space Science Institute (Cassini ISS): mapa de Febe PIA07775 (2005),
  https://photojournal.jpl.nasa.gov/catalog/PIA07775.
- NASA/JPL/Space Science Institute (Cassini ISS): cor de Hipérion medida na composição
  em cor natural PIA08240, https://photojournal.jpl.nasa.gov/catalog/PIA08240; polo e
  taxa de rotação da solução instantânea de 2005 em J. Bauer e P. Thomas,
  "Hyperion: the sponge moon" (JPL/Cornell, 2007).
- NASA/JPL-Caltech/UCLA/MPS/DLR/IDA (Dawn Framing Camera 2), DLR: mosaico global
  HAMO de Vesta (DAWN-A-FC2-5-MOSAIC-V1.0, Roatsch et al.) e DTM estereofotogramétrico
  (DAWN-A-FC2-5-VESTADTMSPG-V1.0, Preusker et al. 2016), e os produtos HAMO
  equivalentes de Ceres (mosaico em filtro claro e DTM estereofotogramétrico), NASA PDS,
  https://sbnarchive.psi.edu/pds3/dawn/fc/.
- NASA/JHUAPL/SwRI/LPI (New Horizons LORRI e MVIC), publicados pela USGS Astrogeology
  (domínio público): mosaicos globais e DEMs de Plutão e Caronte de 300 m (2017), Moore et al.
  (2016) e Schenk et al. (2018), https://astrogeology.usgs.gov/search/map/pluto_new_horizons_lorri_mvic_global_mosaic_300m.
- 67P: cor do núcleo de Fornasier et al. (2015), A&A 583, A30,
  https://doi.org/10.1051/0004-6361/201525901.
- Nereida: albedo, cor e período de rotação de Kiss et al. (2016), MNRAS,
  https://doi.org/10.1093/mnras/stw081 (Kepler/K2, Herschel e Spitzer).

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
