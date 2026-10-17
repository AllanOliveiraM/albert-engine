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
identifica a aproximação ao focar esses corpos. Superfícies são ilustrativas:
há esferas de referência e formas rochosas artísticas descritas abaixo, sem
reutilizar fotografias de outros objetos. Modelos e mapas observados próprios
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

## Formas artísticas dos pequenos corpos

Vesta, Pallas, Halley e 67P usam malhas irregulares individuais. Phobos, Deimos,
Hyperion e Phoebe também recebem formas rochosas ilustrativas. Crateras,
silhuetas, variações de material e microrelevo são procedurais, com semente por
objeto, sem emendas de textura. 67P tem uma silhueta com dois lobos. Não são
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
