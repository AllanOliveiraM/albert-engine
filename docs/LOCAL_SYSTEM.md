# Catálogo local e populações visuais

A interface seleciona objetos já presentes na cena. Não oferece busca nem
importação online. A API gRPC conserva esses recursos para integradores.

São 43 corpos selecionáveis: Sol, oito planetas, 25 luas, cinco planetas anões,
dois asteroides (Vesta e Pallas) e dois cometas (Halley e 67P). As luas adicionais
incluem os quatro grandes satélites de Júpiter, nove de Saturno, cinco de Urano,
Phobos/Deimos, Triton/Nereid/Proteus e Caronte.

O seletor agrupa estrela, planetas, luas, planetas anões, asteroides, cometas e
objetos criados pela API. Nomes aceitam maiúsculas, acentos e pequenas diferenças
de digitação, inclusive duas letras invertidas. Correspondências exatas e
prefixos precedem aproximações; cada categoria desempata pela distância ao Sol.
Satélites usam a distância heliocêntrica do planeta principal e sua órbita para
o desempate. Ao digitar, os resultados de todas as categorias ficam em ordem global de
correspondência, com a categoria identificada em cada linha. Sem consulta,
a lista apresenta seções por categoria.

As vistas Terra/Lua, Internos e Tudo permitem explorar escalas diferentes sem
alterar dimensões físicas. Na vista de um planeta, luas pequenas recebem alvos
de clique em tela; etiquetas atrás de outros corpos ficam ocultas. O clique
direto na geometria continua usando a interseção física mais próxima.

## Posições e limites

Os oito planetas, Terra/Lua/Sol e suas texturas continuam no caminho existente
de efemérides Horizons. Os 33 corpos adicionais usam elementos osculantes
embarcados em `assets/catalog/orbits.json`, referidos a **16/10/2026 00:00 TDB**,
em ICRF, com distâncias em metros e velocidades em metros/segundo.

São elipses de dois corpos, propagadas localmente. Luas são relativas a seus
planetas; os demais são heliocêntricos. Perturbações, precessão e forças não
gravitacionais não são integradas. Não são efemérides precisas em tempo real;
o erro cresce com a distância temporal à época de referência. A interface
identifica a aproximação ao focar esses corpos. Vinte e uma luas adicionais
têm mapas próprios NASA, orientação IAU e detalhes regionais por zoom.
Os demais usam esferas de referência ou formas rochosas artísticas descritas
abaixo, sem reutilizar fotografias de outros objetos. Mais mapas/modelos observados
permanecem no roadmap; os raios nominais são aproximados.

O gerador usa a [API oficial Horizons](https://ssd-api.jpl.nasa.gov/doc/horizons.html)
apenas durante a preparação. Não faz parte do build comum nem consulta a rede
ao pesquisar esses objetos. Respostas originais ficam em `asset-packs/catalog/`,
fora do Git; tabela compacta, proveniência e vetores de referência são versionados.

```sh
python3 scripts/prepare-local-catalog.py
# Regenerar a partir das respostas já obtidas:
python3 scripts/prepare-local-catalog.py --offline
```

O teste compara posição e velocidade de cada órbita na época com uma consulta
independente de vetores Horizons, além de verificar fechamento, limites radiais,
referencial do planeta principal e busca aproximada. Essa comparação valida a
conversão dos elementos; não mede o erro de propagação fora da época inicial.

## Mapas das luas

As fontes e limitações estão em `assets/moon-sources.json`: [Io](https://science.nasa.gov/resource/io-3d-model/),
[Europa](https://science.nasa.gov/resource/europa-3d-model/),
[Ganimedes](https://science.nasa.gov/resource/ganymede-3d-model/) e
[Calisto](https://science.nasa.gov/resource/callisto-3d-model/), NASA VTAD.
Encélado, Tétis, Dione, Reia e Jápeto também usam mapas NASA/VTAD, das páginas
oficiais indicadas no mesmo inventário. Io, Europa e essas cinco luas de Saturno
usam imagens de 4096×2048; Ganimedes/Calisto, 2048×1024. São referências
visuais estáticas, com processamento de cor e cobertura variável, sem DEM.
Não representam atividade atual nem albedo calibrado. O registro da longitude
da textura contra marcos IAU ainda exige validação; a orientação do corpo usa
o modelo IAU `pck00011`, incluindo termos periódicos, testado contra CSPICE.

Os sistemas externos também têm mapas próprios, com **cobertura parcial**:

| Lua | Fonte NASA/VTAD | Resolução da imagem |
| --- | --- | --- |
| Ariel | [Modelo NASA](https://science.nasa.gov/resource/ariel-3d-model/) | 2048×1024 |
| Umbriel | [Modelo NASA](https://science.nasa.gov/resource/umbriel-3d-model/) | 1024×512 |
| Titânia | [Modelo NASA](https://science.nasa.gov/resource/titania-3d-model/) | 2048×1024 |
| Oberon | [Modelo NASA](https://science.nasa.gov/resource/oberon-3d-model/) | 2048×1024 |
| Miranda | [Modelo NASA](https://science.nasa.gov/resource/miranda-3d-model/) | 1024×512 |
| Tritão | [Modelo NASA](https://science.nasa.gov/resource/triton-3d-model/) | 4096×2048 |
| Caronte | [Modelo NASA](https://science.nasa.gov/resource/charon-3d-model/) | 1264×632 |

Áreas uniformes/borradas refletem lacunas ou baixa definição da fonte; não são
observações de terreno liso. A interface avisa ao focar esses corpos. A contagem
de pixels não garante essa resolução em toda a superfície. Não há DEM nem
atividade atmosférica/geológica em tempo real. A orientação de Tritão preserva
a rotação retrógrada e todos os termos periódicos publicados no kernel.

Fobos e Deimos têm mosaicos Viking próprios, em tons de cinza, de 1440×720,
[NASA/JPL/Caltech/USGS](https://space.jpl.nasa.gov/tmaps/mars.html).
A forma usa modelos medidos DSK type 2, mantendo origem, escala e referencial
IAU das fontes. Fobos usa [DLR/NAIF 2014](https://naif.jpl.nasa.gov/pub/naif/generic_kernels/dsk/satellites/phobos_2014_09_22.cmt),
com 137.439 vértices e 274.874 triângulos; Deimos usa o modelo Thomas/Viking
[ESA v0.2](https://spiftp.esac.esa.int/data/SPICE/MARS-EXPRESS/kernels/dsk/DEIMOS_K005_THO_V02.BDS),
com 2.522 vértices e 5.040 triângulos. A versão 0.2 de Deimos corrige a rotação
/ convenção de longitude do produto anterior. Hashes e limites estão no inventário.
Não há deformação procedural ou normalização dessas formas. O raio nominal
permanece como datum do catálogo/LOD; o limite da câmera cobre o raio máximo
medido, aproximadamente 13,923 km e 8,707 km, com margem de 5%.

A orientação inclui os termos periódicos e quadráticos publicados, validada
contra CSPICE. Seleção usa BVH das placas reais, construído fora da thread gráfica.
Recortes de alta resolução substituem somente a imagem, com transição suave,
sem colocar uma esfera sobre o modelo. A forma medida não torna o mosaico Viking
albedo calibrado: ele contém iluminação herdada, lacunas e o registro entre
imagem e marcos IAU da malha ainda não foi validado. Os elipsoides de referência
IAU permanecem como fallback para bibliotecas antigas sem as malhas.

Mimas usa o mosaico global Cassini [PIA18437](https://science.nasa.gov/photojournal/color-maps-of-mimas-2014)
(NASA/JPL-Caltech/SSI/LPI, montagem de Paul Schenk), 6356×3178, cerca de 200 m/pixel
no equador, em **cores realçadas IR–verde–UV**: não é cor natural nem albedo calibrado.
A forma é o modelo medido de [Gaskell](https://sbn.psi.edu/pds/resource/mimasshape.html)
(PDS, imagens Cassini ISS e Voyager 1), versão Q=256: 393.218 vértices, 786.432
triângulos, arestas de 1,0–1,7 km e raio máximo de 210,781 km. Só os vértices
duplicados das arestas do cubo ICQ, sem uso por nenhuma placa, são descartados;
coordenadas e origem originais são mantidas, sem renormalizar a escala.

O mosaico publicado não estava registrado sobre a forma. A correlação entre o
mosaico e o sombreamento sintético da malha, em 84 regiões, mostrou longitude
deslocada em −3,85° e latitude planetográfica: tan(lat publicada) =
(207,8/190,6)² · tan(lat planetocêntrica). A correlação média subiu de 0,04 para
0,52 com a correção, melhorando 83 das 84 regiões. O gerador reamostra o mosaico
com interpolação cúbica para latitude planetocêntrica e longitude leste −180…180.
A mesma malha coincide com 21 crateras do Gazetteer da IAU (mediana de +0,40° em
latitude e −0,34° em longitude), e essas crateras caem sobre o mosaico corrigido.
A causa do deslocamento original não foi estabelecida. Para reproduzir a análise
com as fontes já baixadas (Docker de autoria):

```sh
docker run --rm --user "$(id -u):$(id -g)" -v "$PWD:/work" -w /work \
  --entrypoint python3 albert-assets-builder scripts/register-moon-map.py mimas
```

O referencial da malha usa W0 = 337,46°, e o Albert orienta Mimas pelo pck00011
(333,46°). As longitudes da superfície coincidem com as do Gazetteer, mas a fase
de rotação inercial pode diferir em cerca de 4°. As regiões polares do mosaico são
borradas e a resolução da fonte varia de ~0,2 a 1,6 km/pixel; a interface avisa ao
focar a lua. Não há DEM separado: o relevo vem da própria malha medida.

Febe usa o mapa Cassini ISS [PIA07775](https://photojournal.jpl.nasa.gov/catalog/PIA07775)
(NASA/JPL/Space Science Institute, dezembro de 2005), em tons de cinza, com 8 pixels
por grau (cerca de 230 m/pixel no equador), sobre a forma medida de
[Gaskell](https://sbnarchive.psi.edu/pds4/non_mission/gaskell.phoebe.shape-model/)
(PDS, 208 imagens Cassini ISS), versão Q=128: 98.306 vértices, 196.608 triângulos,
arestas de 1,1–1,8 km e raio máximo de 122,277 km. O mapa tem iluminação embutida e
grandes áreas borradas, vindas de imagens de baixa resolução. Não há observação ao
norte de cerca de 60° N (em parte desde 30° N) nem ao sul de 81° S. O gerador recorta
a moldura e os rótulos e preenche essas áreas (o preto do mapa, o ruído quase preto
ligado a ele e a borda suavizada de 2 px) com a média local das observações vizinhas,
em cerca de 2°, que se dissolve no cinza mediano. Nenhum detalhe é inventado: ali, o
relevo vem só da malha. Manchas cinzentas uniformes no sul são o preenchimento dos
próprios autores do mapa.

O registro foi conferido pela mesma correlação com o sombreamento sintético da malha,
nas 58 regiões sem lacunas: correlação média de 0,109 com a grade publicada. Os
melhores modelos de uma varredura ampla (k de 1,00 a 1,05, longitude +1° a +2°) chegam
a 0,118, sem diferença significativa num teste pareado de dois erros-padrão. A grade
publicada foi mantida, com incerteza de cerca de 2° (4 km). As crateras do Gazetteer,
de 30 a 100 km num corpo de 213 km, dispersam cerca de 5° no ajuste das bordas e não
restringem o registro. A malha usa o mesmo meridiano e a mesma rotação do pck00011;
o polo difere 0,08° em declinação. `scripts/register-moon-map.py phoebe --search`
reproduz a análise.

Hipérion usa o modelo de placas de [Thomas](https://doi.org/10.26033/ewy3-jy61)
(PDS, imagens Cassini ISS): 14.636 vértices, 29.268 triângulos, arestas de 3,4–6,0 km,
raio máximo de 184,616 km e volume equivalente a uma esfera de 136,25 km. A incerteza
é inferior a 1 km no lado melhor imageado e chega a 6 km no oposto; crateras pequenas
não estão representadas de forma confiável. **Não há mapa de superfície**: a malha
recebe uma cor uniforme, medida no disco iluminado da composição Cassini em cor
natural [PIA08240](https://photojournal.jpl.nasa.gov/catalog/PIA08240) e escalada para
albedo geométrico de ~0,3 (as estimativas publicadas vão de 0,2 a 0,44). O relevo e as
sombras vêm só da forma medida; o contraste real de albedo (material escuro no fundo
das crateras) não aparece. O modelo 3D da NASA/VTAD foi avaliado e descartado: depois
do melhor ajuste de escala, rotação e translação sobre as placas de Thomas, a malha
dele só reproduz a forma geral (resíduo radial mediano de 4,4 km, RMS de 8,2 km), e
nem o relevo nem a textura dele se correlacionam com o relevo medido (0,02–0,05 em 84
regiões, contra 0,999 no autoteste); suas crateras cairiam fora das reais.

A rotação de Hipérion é caótica, e o pck00011 não traz modelo de orientação. A
orientação do Albert é **ilustrativa**: mantém fixa a solução instantânea medida pela
Cassini no sobrevoo de 25–26/09/2005 (polo em AR 299,8°, Dec 58,7°, 72°/dia; J. Bauer
e P. Thomas, 2007), que define o eixo +Z do próprio modelo de Thomas, e ancora a fase
no ponto subsolar da imagem N1506385183, listado no documento PDS do modelo. Ela
reproduz a latitude subsolar observada com diferença de 0,2° e as longitudes ao longo
daquele sobrevoo com 0,01°; em outras datas, a orientação real difere arbitrariamente.
`python3 scripts/anchor-orientation.py hyperion` reproduz a fase e confere a direção
do Sol pelas outras soluções publicadas.

Proteu usa o modelo de [Stooke](https://doi.org/10.26033/yt84-5y91) (PDS), feito a
partir de limbos e terminadores das imagens da Voyager 2 e retocado em grandes crateras
e cristas: um raio a cada 5° de latitude e longitude (cerca de 17 km no equador). O
Albert triangula só os pontos publicados (quadriláteros em dois triângulos, leques nos
polos): 2.522 vértices, 5.040 triângulos, raio máximo de 232,5 km. O volume equivale a
uma esfera de 201,0 km, menor que os 210 ± 7 km estimados por Karkoschka (2003); o
autor observa que esses modelos podem exagerar a profundidade das depressões. A
origem da grade não coincide com o centro de figura e foi mantida. **Não há mapa de
superfície**: a Voyager 2 imageou só parte de Proteu, e não há mosaico global nem modelo
3D da NASA. A malha recebe cinza neutro uniforme (cor plana entre 0,4 e 0,6 µm, Thomas e
Veverka, 1991) com albedo geométrico de 0,096, o valor mais citado; estimativas da época
da Voyager, sem o efeito de oposição, davam cerca de 0,06. A orientação segue o
pck00011, com os termos periódicos de Netuno, validada contra CSPICE.

Nereida **não tem forma medida nem imagem resolvida**: a Voyager 2 só a observou a cerca
de 4,7 milhões de km. O Albert a mostra como esfera de 340 km (Kiss et al. 2016, Kepler/K2
com Herschel e Spitzer: 335–345 km; Voyager: 350 ± 50 km), em cinza neutro com albedo
geométrico de 0,25 ± 0,02 do mesmo estudo, cuja inclinação espectral é próxima do grupo
neutro-escuro de Centauros e objetos transnetunianos. As curvas de luz admitem
alongamento de no máximo 1,3:1, então o contorno real pode diferir em dezenas de
quilômetros. Não há modelo IAU de rotação: o período de 11,594 h vem do K2, e o polo
(o de Netuno) e a fase são arbitrários, sem efeito visível numa esfera uniforme. O
painel avisa essas limitações.

Titã aparece por padrão como em **luz visível**: uma névoa opaca alaranjada, sem
superfície visível. A cor vem da cromaticidade do disco iluminado na composição em
cor natural Cassini [PIA06230](https://pds-rings.seti.org/press_releases/pages/PIA06xxx/PIA06230.html)
(filtros vermelho/verde/violeta, fase 56°), escalada para albedo geométrico visível
de 0,22. A névoa espalha luz além do terminador geométrico, e uma casca translúcida
reproduz a névoa principal no limbo (altura de escala de 60 km) e a camada alta
destacada, azulada, perto de 400 km, com espalhamento frontal Henyey-Greenstein
(g = 0,65): tênue em fase moderada, um anel brilhante contra o Sol, como nas imagens
[PIA17180](https://photojournal.jpl.nasa.gov/catalog/PIA17180). É um modelo visual
aproximado, não transferência radiativa; a altitude da camada variou com as estações.

A opção **Superfície em infravermelho (938 nm)**, no painel de Titã, mostra o mosaico
global Cassini ISS [PIA22770](https://photojournal.jpl.nasa.gov/catalog/PIA22770)
(NASA/JPL-Caltech/University of Arizona, 2018): albedo normal calibrado em 938 nm,
5760×2880, 16 pixels por grau, visto através da névoa no infravermelho próximo — não
é a aparência a olho nu. O registro foi conferido em crateras e lagos do Gazetteer
(Menrva, Sinlap, Selk, Ksa, Hano, Afekan, Ontario Lacus, Kraken e Ligeia Mare) sem
correção além de recentrar a longitude. Os detalhes regionais só são carregados
nesse modo; não há DEM. A orientação usa pck00011.

O build comum já reúne `assets/imagery/moons/` à biblioteca única. A versão completa
não baixa esses mapas; a leve traz o nível de 1024 px de cada lua no container
essencial e baixa os níveis seguintes em `moons-2` e `moons-3`. Cada base usa até 1024 pixels de largura; o recorte de
maior resolução entra suavemente ao aproximar e é liberado após três segundos
longe ou fora de foco. Não se mantém uma cópia integral em alta resolução de
todas as luas na RAM. O pacote adicional (luas e pequenos corpos) ocupa **77.586.518 bytes**.

Para regenerar (opcional, exige Docker): `make assets-moons` no Linux ou
`.\make.cmd assets-moons` no Windows. Para usar originais já baixados:
`bash scripts/prepare-assets.sh --moons --offline`. Originais e intermediários
ficam fora do Git em `asset-packs/moons/`; o pacote otimizado e os hashes são
versionados. O processo preserva os pacotes de Terra/Lua/Sol e planetas.

Fobos/Deimos já têm mapas, modelos medidos e orientação IAU;
faltam validar o registro das imagens Viking, como já feito para Mimas, e melhorar
as órbitas.

## Vesta

Vesta usa os produtos da câmera FC2 da sonda Dawn processados pelo DLR e publicados no
PDS: o mosaico global HAMO em filtro claro (Roatsch et al.; ~60 m/pixel na origem) e o
modelo digital de terreno estereofotogramétrico (Preusker et al. 2016; espaçamento de
~70 m e precisão vertical de ~6 m), ambos no sistema IAU "Claudia double-prime", o
mesmo da rotação do pck00011. O gerador faz média de área do mosaico para 64 pixels por
grau (~70 m) e do relevo para 32 pixels por grau (~140 m), em longitude leste −180…180,
e ambos ganham pirâmides com detalhe regional por zoom, como Mercúrio. As alturas são
desenhadas sobre a esfera do raio de catálogo (262,7 km): os raios medidos vão de
212 a 293 km, com a bacia Rheasilvia no sul e o pico central a ~20 km acima do
elipsoide. O mosaico mantém a iluminação das imagens originais e não é albedo
calibrado. A distância mínima da câmera usa o raio máximo medido (293,0 km).

Ceres segue o mesmo caminho, com os produtos HAMO do DLR no sistema IAU "Kait" (o do
pck00011): mosaico em filtro claro reamostrado para 60 pixels por grau (~140 m) e
relevo para 30 pixels por grau (~270 m), sobre a esfera do raio de catálogo
(469,7 km); os raios medidos vão de 442 a 487 km. O mosaico tem contraste realçado
(as faculas de Occator saturam) e mantém a iluminação original; as áreas polares
nunca imageadas, sobretudo ao sul de 80° S, recebem a média local das observações
vizinhas dissolvida no cinza mediano, e o painel avisa.

## Cometa 67P/Churyumov-Gerasimenko

A 67P usa o modelo de forma da câmera OSIRIS da Rosetta (estereofotoclinometria
LAM-PSI), na versão de 194.668 placas distribuída como DSK pelo serviço SPICE da
ESA (`ROS_CG_K195_OSPCLPS_N_V1.BDS`, espelho NAIF): arestas de ~23 m, raio máximo de
2.613 m e volume equivalente a uma esfera de 1.681 m (o catálogo usa 1.650 m).
A decimação deixou 13 "aletas" de espessura zero (um triângulo e o mesmo invertido,
presos por uma aresta); elas e os 13 vértices que só elas usavam são descartados, e
nada mais muda. Sem mapa de albedo: cor uniforme do núcleo medida pela OSIRIS
(albedo geométrico de 6,5% a 649 nm e inclinação espectral de 11%/100 nm em fase
baixa; Fornasier et al. 2015). A orientação segue o pck00011 (polo e período de
12,404 h da época Rosetta); o período encurtou nas passagens pelo periélio de 2015 e
2021, então a fase de rotação atual não é uma previsão. Coma e cauda ainda não são
desenhadas.

## Cometa 1P/Halley

Halley usa o modelo de [Stooke](https://doi.org/10.26033/yt84-5y91) feito com limbos e
terminadores das imagens da Giotto e das Vega, com apontamento de A. Abergel: raio a
cada 5°, longitude leste (convenção de cometas), eixo z ao longo do eixo longo. São
15,1 km no eixo longo e cerca de 7,5 km nos outros; o volume equivale a uma esfera de
4,58 km de raio (o catálogo usa 5,5 km). O próprio autor considera o modelo
extremamente incerto (erros absolutos de 0,5–1 km). Cor uniforme com o albedo de ~0,04
medido pela câmera da Giotto (Keller et al. 1986). Em 1986 o núcleo girava em estado
excitado (Belton et al. 1991: precessão de ~3,7 dias e giro de ~7,1 dias em torno do
eixo longo), que não pode ser propagado até hoje; a orientação do Albert é
**ilustrativa**: giro uniforme de 7,1 dias em torno do eixo longo, apontado para o polo
da eclíptica. Coma e cauda ainda não são desenhadas.

## Formas artísticas dos pequenos corpos

Pallas usa uma malha irregular individual: sem sonda, as dimensões publicadas a partir
de óptica adaptativa ainda não foram verificadas para uso aqui. Fobos, Deimos, Febe,
Hipérion, Proteu, a 67P e Halley usam o modelo medido acima quando a biblioteca contém as malhas.
Numa biblioteca antiga sem as malhas, usam o elipsoide IAU (Hipérion, uma esfera)
sob o mapa; sem nenhum asset do corpo, a forma rochosa ilustrativa. Crateras,
silhuetas, variações de material e microrelevo são procedurais, com semente por
objeto, sem emendas de textura. Não são
reconstruções fotogramétricas nem modelos de missão; a interface deixa essa
distinção explícita. A orientação dessas formas também não é uma atitude medida.

A geração conserva o volume equivalente ao raio nominal do catálogo e centra
a malha no centro de volume. A câmera respeita a extensão visual da malha para
não entrar nas pontas dos corpos alongados. Cada superfície tem 20.480 triângulos
e cerca de 1,9 MiB de atributos; não há texturas adicionais residentes nem
download. O microrelevo reduz frequências que ficariam menores que um pixel.
Substituir essas formas por modelos observados, onde disponíveis, continua no
roadmap com alta prioridade junto à fidelidade das luas.

Nas vistas de conjunto, rótulos e órbitas de corpos secundários desaparecem
suavemente quando se aglomeram na tela ou ficam muito distantes. Histerese
evita alternância perto do limiar. Ao aproximar, eles reaparecem quando há
espaço; a seleção pelo catálogo continua disponível. Sol e planetas mantêm
suas marcações.

## Cinturões e nuvem de Oort

A cena contém populações determinísticas de 6000 partículas no cinturão
principal, 4500 no cinturão de Kuiper e 2400 na nuvem de Oort. O tamanho em pixels
e a visibilidade são recursos didáticos: partículas não são asteroides reais
cadastrados, e suas densidades aparentes não representam uma fotografia do espaço.
A distribuição é estática e ilustrativa; não simula colisões nem dinâmica coletiva.

O cinturão principal ocupa uma faixa aproximada de 2,1–3,3 UA, Kuiper de 30–50 UA,
e a nuvem esférica de Oort de 2000–50000 UA. Tornam-se visíveis gradualmente ao
afastar a câmera, sem aumentar o zoom padrão nem o catálogo selecionável. O menu
permite desligá-los. A nuvem de Oort exige afastamento muito maior que a vista
padrão dos planetas. A câmera permite essa escala sem carregar imagens adicionais.

Referências: [NASA — cinturão de asteroides](https://science.nasa.gov/solar-system/asteroids/),
[Kuiper](https://science.nasa.gov/solar-system/kuiper-belt/),
[Oort](https://science.nasa.gov/solar-system/oort-cloud/).
