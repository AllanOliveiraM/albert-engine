# Física de voo

Núcleo sem renderização em `src/physics` (`albert_physics`, só depende de ERFA):
corpo rígido de seis graus de liberdade com massa variável, da superfície à órbita.
Estado e cálculo em `double`, SI, passo fixo; nada depende do FPS.

## Modelos

| Área | Modelo | Limite conhecido |
| --- | --- | --- |
| Tempo | TT em nanossegundos inteiros desde J2000; UTC por `eraDat` | antes de 1960 TAI−UTC = 0 |
| Referenciais | GCRF inercial; ITRF por IAU 2006/2000A (CIO) + ERA, matriz C2I a cada 60 s interpolada | movimento do polo ignorado (≤15 m); TT−UT1 fixo por voo |
| Gravidade | EGM2008 tide-free em harmônicos esféricos (recursão de Cunningham), 20×20 por padrão, até 70×70; elipsoide WGS 84 | maré permanente não aplicada; graus acima de 70 omitidos |
| Terceiros corpos | Sol (epv00) e Lua (moon98), aceleração de maré, 60 s interpolados; termo relativístico de Schwarzschild do corpo central (IERS 2010, 10.12) | sem planetas; sem Lense-Thirring nem de Sitter |
| Atmosfera | NOAA GFS 0,25° ao vivo (41 níveis isobáricos até 0,01 hPa ≈ 80 km: altura geopotencial, temperatura, vento, umidade específica), interpolado no tempo, na horizontal e na vertical; acima (transição de 8 km) e fora do recorte de ±6° (transição de 0,5°), NRLMSISE-00 até 1000 km com F10.7 e Ap da CelesTrak (observados ou previstos) e ventos horizontais do HWM14 (climatologia do solo a ~450 km mais os ventos de perturbação geomagnética com o ap de 3 h); sem índices, US Standard Atmosphere 1976 (vácuo acima de 86 km); co-rotativa | NRLMSIS 2.x excluído pela licença (uso acadêmico, sem modificação); HWM14 é climatologia (sem fluxo solar; perturbação média, constante acima de ~225 km); onde o NRLMSISE-00 vale sozinho (fora do recorte ou sem GFS) o vento perto do solo também é do HWM14 |
| Vento | do GFS no veículo, do HWM14 acima dele (mesma transição de 8 km); a velocidade do ar desconta vento e rotação da Terra | rajadas e turbulência não modeladas |
| Aerodinâmica | tabelas bilineares (Mach, ângulo); arrasto oposto ao ar relativo, sustentação no plano x-z, força lateral em y; amortecimento opcional | fora da tabela o valor é mantido |
| Propulsão | `F = F_vac·throttle − p_a·A_e`; `ṁ = F_vac·throttle/v_e`; cardã em arfagem e guinada | sem transiente de ignição |
| Massa | propriedades do conjunto lineares no propelente restante; estagiamento move o CG | inércia principal (sem produtos) |
| Rotação | `I ω̇ = M − ω×Iω − İω − ṁ l² ω⊥` (perda de inércia do propelente e amortecimento por jato) | — |
| Lua e Marte | `Environment environment(Center, campo, grau)`: centro inercial no corpo com eixos ICRF; GRAIL GRGM1200A (Lua, eixos principais DE440) e MRO GMM-3 (Marte) baixados até o grau pedido (≤1200 e ≤120); rotação IAU de `pck00011` com ω analítico (Lua: referencial médio da Terra com librações, o dos mapas; o campo é avaliado nos eixos principais DE440, 67,8526″/78,6944″/0,2785″ adiante; Marte: IAU 2015); terceiros corpos Terra+Sol (Lua) e Sol por `plan94` (Marte); Marte com atmosfera média hidrostática sobre as temperaturas da NASA Glenn até 140 K perto de 50 km, isotérmica acima e vácuo acima de 150 km; Lua sem ar | série IAU da Lua a ≤155 m da libração do DE na superfície; Marte sem estações, poeira nem vento; alturas sobre elipsoide (esfera de 1737,4 km na Lua; 3396,19 × 3376,20 km em Marte), não sobre o areoide |
| Aquecimento | convectivo no ponto de estagnação, Sutton-Graves (k do ar e do CO2 marciano, `Stage::noseRadius`), carga térmica integrada no RK4 | sem radiação do choque (>10 km/s), sem ablação nem condução |
| Radiação solar | pressão de esfera (`Vehicle::sunArea`, `reflectivity`; 1361 W/m² a 1 au) com umbra e penumbra cônicas do corpo central | desligada por padrão; sem albedo nem infravermelho do planeta; atitude ignorada |
| Solo | pad com a base do conjunto no solo, preso girando com o corpo até empuxo > peso; solo a uma altura elipsoidal fixa (do relevo medido com `terrain_height`); pernas de pouso (`Stage::legs`) como molas-amortecedores com atrito de Coulomb regularizado (linear abaixo de 5 cm/s), força e momento em cada pé; sem pernas, toque detectado no CG e o veículo para | solo plano em torno do sítio; sem deformação do regolito nem rebote do corpo |

Integração RK4 com passo fixo (1 ms por padrão); `advance` divide o passo no instante
exato do fim do propelente e para ali (e também na liberação do pad e no toque), para que
estagiamentos programados sejam exatos. Em voo balístico acima da atmosfera (sem empuxo nem torque
comandado, fora do pad, com margem de altura para o passo) o passo cresce até 1 s, limitado a 0,05 rad
de rotação por passo: uma volta em órbita fica a <1 mm do resultado com 10 ms e roda dezenas
de vezes mais rápido.
A mesma sequência de passos reproduz o estado bit a bit (`Flight::hash`); igualdade
entre plataformas e compiladores não é prometida.

Sensores: `Flight::increments()` integra no RK4 a força específica e a taxa angular no corpo
(incrementos de velocidade e ângulo de um IMU strapdown). `Imu` aplica os erros da IEEE 952 por
eixo — ruído branco (VRW/ARW), instabilidade de viés como Gauss-Markov, passeio aleatório da
taxa, viés e fator de escala sorteados uma vez — e erros de barômetro, com ruído determinístico
e portátil (xoshiro256** + Box-Muller); graus `ideal`, `tactical` e `industrial` com valores
representativos de catálogo, não de um produto.

Sensores ideais em `Flight::sample()`: força específica no corpo (o que um IMU mede:
zero em queda livre, +g no pad), taxas angulares, pressão estática, ar, Mach, pressão
dinâmica, α/β, ângulos de Euler em NED e estado verdadeiro (inercial, ECEF, geodésico).

## Validação (`tests/physics_test.cpp`)

- Casos NESC 7 (vento constante) e 8 (cisalhamento): milímetros das referências.
- GFS: decodificação GRIB2 conferida com decodificador independente; a análise de
  2026-10-17 12 UTC interpolada contra a radiossondagem de Tampa (72210, U. Wyoming) em 14
  níveis de 925 a 10 hPa: mediana de |ΔT| 0,38 K, máximo 2,4 K, vento RMS 3,0 m/s,
  pressão ≤0,4% (a análise assimila a sondagem). Independente: a previsão de 18 UTC +6 h contra
  a sondagem de 2026-10-18 00 UTC em 10 níveis (925 a 100 hPa): viés −0,39 K, mediana de |ΔT|
  0,64 K, máximo 2,8 K, vento RMS 3,0 m/s, pressão ≤0,23%.
- HWM14 (`tests/hwm14_test.cpp`): as 740 saídas de `Check/gfortran.txt` da distribuição do NRL (perfis de
  altura, latitude, hora local, longitude, dia do ano e ap, e os três perfis do DWM07) com diferença máxima
  de 0,0006 m/s (impressas com 3 casas); acima do GFS o vento é o do HWM14 (antes, zero).
- NRLMSISE-00: os 17 casos de referência da distribuição do NRL nos 7 algarismos publicados;
  índices da CelesTrak (F10.7 do dia anterior, média de 81 dias, Ap diário e histórico de 3 h).
- EGM2008 contra o gradiente do potencial somado com funções de Legendre normalizadas diretas
  (graus 20 e 70, <2e-7 m/s²) e alturas geoidais por Bruns: 28,9 m em Timbuktu (exemplo
  do GeographicLib, 28,7 m) e −105,1 m no mínimo do Oceano Índico (≈ −106 m).
- Geodésia fechada de Vermeille contra a transformação direta (20 mil pontos, <1 µm);
  gravidade zonal contra o gradiente numérico do potencial.
- USSA76 contra a tabela publicada (5 algarismos) e contra os valores das simulações NESC.
- Dois corpos: 10 revoluções contra a equação de Kepler (<1 mm), energia relativa <1e-10.
- Orientação IAU contra `eraC2t06a` (<2e-10); TT−UTC em 2017 (69,184 s).
- NASA NESC 6-DOF check cases (NASA/TM-2015-218675), 30 s: esfera sem arrasto (1), Terra
  redonda sem rotação (4), elipsoide WGS 84 com rotação (6), balas de canhão para leste e
  norte (9, 10). Posição a ≤1 cm e velocidade a ≤1 mm/s da referência mais próxima
  (milímetros na prática).
- NESC 17, foguete de dois estágios até 250 km em 200 s: dentro da divergência entre as
  duas simulações de referência (5,9 m da sim 5 e 11 m da sim 4; elas diferem 6,6 m).
  Reproduzir esse caso exige a convenção das referências: metade do termo İω e sem
  fluxo de momento angular do jato (`Vehicle::inertiaRate`, `jetDamping`). Reduzir o
  passo à metade move o ponto final 0,22 m (critério <1 m).
- Taxa secular do nodo por J2 a 51,6° e 98° (heliossíncrona): <0,1% da teoria de primeira ordem (Vallado).
- Esgotamento exatamente no fim do passo; equação do foguete (Tsiolkovsky) a 1e-7 m/s;
  amortecimento por jato e perda de inércia contra a solução fechada `Iω = H0 (I/I0)^(ṁ l²/|İ|)`.
- IMU: variância de Allan sobreposta de um giro parado (10 Hz, 20 mil s) ajustada por
  σ²(τ) = N²/τ + K²τ/3 devolve ARW e RRW configurados a ±5% (1,008e-3 e 9,80e-6 para 1e-3 e
  1e-5); Gauss-Markov com dispersão e correlação configuradas a ±5%; incrementos de Δv e Δθ
  contra as soluções fechadas do caso com amortecimento por jato.
- Pad (base no solo), liberação no empuxo, acelerômetro e pouso.
- O guiamento de exemplo leva o lançador genérico a uma órbita ~182 × 212 km.
- Coast de uma volta com passos de 1 s contra 10 ms: diferença <1 mm.

Lua e Marte (`tests/central_bodies_test.cpp`, com os excertos de grau 8 das tabelas do PDS):

- Leitura por faixa (5612 bytes até o grau 8), cache, recarga de cache corrompido,
  coeficientes não finitos e tabela truncada rejeitados.
- Gravidade na superfície: 1,624 m/s² na Lua e 3,724 m/s² no equador de Marte; o campo tende
  à massa pontual longe do corpo.
- Orientação de Marte contra CSPICE `pxform(IAU_MARS, J2000)` com `pck00011` em três épocas de
  2000 a 2100 (<2e-9); rotação entre os eixos principais da Lua e o referencial IAU de 103,91″
  com as componentes da NAIF; ω contra a derivada numérica da matriz (<1e-7).
- Libração: o ponto subterrestre fica a ≤6,1° de longitude e ≤6,6° de latitude em um mês.
- Atmosfera marciana: 699 Pa, 242,1 K e 0,0150 kg/m³ na superfície, contínua na camada
  isotérmica, densidade decrescente até o vácuo. Contra as entradas das Viking 1 e 2 (Seiff e
  Kirk 1977, PDS Atmospheres Node), erro médio de densidade de 23% e 51% em todos os níveis de
  120 km à superfície (pior 41% e 89%); a Viking 2 entrou numa atmosfera mais fria, e um perfil
  médio único não chega aos 20% do critério da fase 5 — isso pede estação e latitude
  (Mars-GRAM ou MCD). O ajuste da NASA Glenn usado antes errava 73% e 115%.
- Órbitas circulares de 100 km na Lua e 300 km em Marte: uma volta com passos de 1 s a <1 µm
  de 0,1 s.

Contra órbitas reais (estado inicial do JPL Horizons, ICRF, trajetórias reconstruídas,
propagação de 6 h com passos de 1 s, sem arrasto nem radiação solar):

- LRO, 2026-03-10 00–06 TDB, 83–100 km de altitude, 3 voltas: 3,7 m do Horizons com
  GRGM1200A no grau 300, 8,3 m no 100, 202 m no 50 e 894 m no 20; sem a Terra e o Sol, 368 m.
- LRO em 24 h (2026-03-10, grau 150): 35 m, dentro do critério de 100 m/dia da fase 5.
- MRO, 2026-09-10, 238–291 km: 37 m em 6 h e 458 m em 24 h com GMM-3 no grau 120, quase todo
  ao longo da trajetória e em degraus a cada volta. A pressão de radiação solar com sombra
  (Cr·A/m de 0,02 a 0,06 m²/kg) muda o resultado em menos de 1 m; o degrau por volta é arrasto
  no periapsis (~255 km), na termosfera que o modelo não tem acima de 150 km.
- Relatividade: avanço do periapsis de 6πGM/(c²a(1−e²)) por volta (40 voltas a 8000 km, e = 0,2:
  4,3541e-7 rad, igual à teoria em 5 algarismos).
- Aquecimento: fórmula de Sutton-Graves exata na amostra (77,7 W/cm² a 60 km e 7 km/s com raio de
  1,83 m), carga térmica de 1 s a 5%, nula no pad e no vácuo.
- Pernas de pouso (`genericLander` na Lua): repouso com a deflexão estática m g/4k (6,84 mm,
  <0,2 mm), queda de 2 m que perde energia e assenta no mesmo ponto, deslizamento a 1 m/s que
  para em 0,511 m (v²/2μg = 0,513 m) e decolagem com o motor.
- Radiação solar: sol pleno, umbra e penumbra monotônica atrás da Terra; no sol, o
  acelerômetro lê P (1 au/d)² Cr A/m oposto ao Sol (<1e-9).

## API

`FlightService` (gRPC, lockstep) expõe o voo a software externo e espelha o veículo na
cena; ver [RPC.md](RPC.md#voo-físico-em-lockstep).

## Veículos

- `genericLander`: estágio único do porte da descida do módulo lunar Apollo (15 t, 45 kN com
  estrangulamento até 10%, Isp 311 s), quatro pernas a 4,7 m do eixo; aproximado.
- `genericLunarAscent`: estágio de subida do porte do módulo lunar Apollo (4,85 t com 2.353 kg de
  propelente, motor fixo de 15,57 kN a Isp 311 s, atitude por RCS ~1,4 kN m); aproximado. Com a
  guiagem de exemplo sai da Base da Tranquilidade para 18 × 85 km em 434 s (Apollo 11: 435 s,
  17 × 88 km), com 140 kg de sobra.
- `genericMarsAscent`: veículo de subida de Marte de dois estágios na massa dos estudos do MAV do
  Mars Sample Return (~450 kg, 0,57 m, 3 m), com motores líquidos estranguláveis (8 kN a Isp 310 s;
  2,5 kN a Isp 320 s), ~4,6 km/s; aproximado. De Jezero: guiado desde a decolagem até um apoapsis
  de 250 km, plana até ele e circulariza a 200 km de perigeu (~201 × 251 km, 12 kg de sobra).
- `nescSphere`, `nescTwoStage`: modelos DAVE-ML da NESC convertidos para SI. O foguete da
  NESC é conceitual ("provavelmente inviável") e sem guiamento.
- `genericLauncher`: lançador médio de dois estágios a querosene dimensionado por números
  públicos aproximados do Falcon 9 v1.2 (massas, empuxo, Isp, geometria); aerodinâmica de
  corpo esbelto com escoamento cruzado de Allen-Perkins. Aerodinamicamente instável: exige
  controle ativo. Não representa nenhum veículo real com precisão.
