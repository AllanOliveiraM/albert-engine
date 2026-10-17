# Eclipses

O Albert calcula automaticamente o bloqueio do Sol entre Terra e Lua com as
posições da simulação. A opção **Sombras**, nas vistas da Terra e da Lua, controla o efeito.
Não é necessário baixar imagens de eclipses nem ativar um evento artificial.

O Sol tem raio finito: a interseção entre seu disco angular e o disco do corpo
ocultante determina a fração de luz direta em cada ponto da superfície. Isso
permite umbra, penumbra e antumbra, inclusive um eclipse solar anular. O modelo
também atende às nuvens observadas e às malhas regionais da Terra e da Lua.
Camadas compartilham a geometria existente, sem duplicar imagens ou DEMs; o
passe de eclipse fica oculto quando não há alinhamento possível.

Durante o eclipse lunar, uma transmissão vermelha fraca representa luz solar
filtrada/refratada pela atmosfera terrestre. A cor e a intensidade são uma
**aproximação visual**, não uma previsão do estado real da atmosfera nem da
escala de Danjon. Não foram modelados aerossóis, vulcanismo, cobertura de nuvens
ao redor do limbo, espectro completo ou refração tridimensional. A luz auxiliar,
quando ativada pelo usuário, continua sendo um recurso de visualização.

Limites geométricos: discos circulares e Sol uniformemente brilhante; sem
escurecimento do limbo solar, relevo do limbo lunar ou forma elipsoidal completa
da sombra terrestre. As efemérides determinam o alinhamento, mas este render
não substitui um calculador científico de contatos e trajetos de eclipses.
O efeito novo cobre Terra–Lua; eclipses mútuos de outras luas ainda precisam
ser generalizados. Atualmente a simulação acompanha o instante real: controles
de data e reprodução histórica continuam no roadmap.

Validação inclui totalidade, eclipse parcial, anular, corpo atrás do Sol,
continuidade da penumbra e cenários Terra/Lua. Regressões OpenGL verificam o
escurecimento solar na superfície terrestre, a coloração lunar e a desativação
do efeito. São cenários geométricos sintéticos, não uma comparação fotométrica
com observações de um eclipse específico.

Referências: [NASA — eclipses e Lua](https://science.nasa.gov/moon/eclipses/),
[geometria dos eclipses](https://science.nasa.gov/eclipses/geometry/) e
[escala de Danjon](https://eclipse.gsfc.nasa.gov/OH/OHres/Danjon.html).
