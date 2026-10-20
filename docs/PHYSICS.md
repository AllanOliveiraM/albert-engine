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
| Vento | do GFS no veículo, do HWM14 acima dele (mesma transição de 8 km); a velocidade do ar desconta vento e rotação da Terra; turbulência de Dryden opcional (MIL-F-8785C: u de primeira ordem, v e w pelo filtro de dois polos, escalas e intensidades de baixa altitude pela altura sobre o solo, 1750 ft acima de 2000 ft; intensidade vertical σ escolhida, semente) nos eixos da velocidade do ar, constante em cada passo | intensidade dada pelo usuário (sem tabela de probabilidade de excedência); sem von Kármán nem rajadas discretas |
| Som | `physics/acoustics`: fonte pelo NASA SP-8072 (potência η ½ F Ue com η 0,35%, espectro da Fig. 5 no número de Strouhal f d_e/Ue, d_e da área de saída do estágio), lóbulo de diretividade de +4,8 dB a 65° da pluma (−3 dB a ±20°, normalizado na esfera), terços de oitava de 1,26 Hz a 15,8 kHz; propagação no referencial girante: tempo retardado sobre o histórico de emissões, absorção ISO 9613-1 somada em 8 pontos do caminho com o ar do voo (umidade do GFS; sem ela, 50%), velocidade do som média do caminho, Doppler pelas velocidades em relação ao ar, reflexão no solo duro (+6 dB a +3 dB) abaixo de 100 m | caminho reto (sem refração), sem difração; sem crepitação (não linear) nem ruído aerodinâmico; impedância constante ao longo do caminho; só na Terra |
| Aerodinâmica | tabelas bilineares (Mach, ângulo); arrasto oposto ao ar relativo, sustentação no plano x-z, força lateral em y; amortecimento opcional; superfícies de controle (`Stage::surfaces`: limite e taxa da deflexão) somam incrementos dos coeficientes em tabelas trilineares (Mach, α ou β, deflexão); uma superfície posicionada aplica sua sustentação, arrasto e força lateral onde está, e o momento sai daí (o arrasto de um flap guina e rola o veículo) | fora da tabela o valor é mantido; superfícies sem interferência entre si |
| Controle de reação | propulsores (`Stage::rcs`: posição, direção, empuxo, v_e) no propelente do estágio; com eles, o comando de torque vira ciclos de trabalho por mínimos quadrados em caixa (conjunto ativo primal exato: o torque, depois o menor consumo, depois binários), com saturação, consumo e a força que sobra; ciclos também diretos por propulsor; sem eles, o torque é ideal (`Vehicle::torque`) | ciclo de trabalho como média (sem pulsos mínimos); alocação refeita a cada comando, no CG do momento |
| Motores a ar | jatos (`Engine::breathing`): empuxo em plena aceleração e consumo específico (kg/N/s) em tabelas de Mach × altitude, nada sem ar, o carretel (spool) segue a aceleração com uma constante de tempo; o combustível é o propelente do estágio | sem modelo termodinâmico do ciclo (tabelas do usuário); sem efeito do ângulo de ataque na entrada |
| Água e estruturas | `Environment::water`: a superfície do sítio é água do mar; o conjunto como cilindro em 20 fatias, cada uma com empuxo pela fração molhada (a extensão vertical da fatia na inclinação), arrasto quadrático e amortecimento de afundamento por ondas (~ρ A √(g R)); sem pouso rígido. `Environment::structures`: segmentos fixos no corpo (braços de captura) com raio, mola, amortecedor e atrito, tocados pelos pontos rígidos dos estágios (`Stage::hardPoints`); testados contra o empuxo exato de 2 m de cilindro, o equilíbrio flutuando e o repouso no braço a mg/k | casco cilíndrico (sem forma de cápsula); sem ondas nem correntes; estruturas sem massa nem movimento |
| Rodas | trem de pouso (`Stage::wheels`): amortecedor de mola por roda, atrito separado ao longo do rumo da roda (rolamento, ou frenagem comandada) e através dele (curva), direção comandada; física testada contra μ g de rolamento e frenagem e a curva cinemática | pneus sem deriva modelada além do atrito regularizado; pista plana na altura do sítio |
| Propulsão | por motor (`Stage::engines`: posição, direção, empuxo no vácuo, v_e, área de saída, aceleração mínima, curso e taxa do cardã): `F = F_vac·throttle − p_a·A_e`, `ṁ = F_vac·throttle/v_e`, força na direção do motor girada pelo cardã (guinada em z, depois arfagem em y) e momento r × F do bocal; comandos a todos ou a um motor, falha (motor apagado que não reacende); o atalho de um motor no eixo (`thrust`, `nozzle`…) é esse motor | sem transiente de ignição; tanque único por estágio |
| Massa | propriedades do conjunto lineares no propelente restante; estagiamento move o CG | inércia principal (sem produtos) |
| Rotação | `I ω̇ = M − ω×Iω − İω − Σ ṁᵢ lᵢ² ω⊥` (perda de inércia do propelente e amortecimento por jato de cada motor) | — |
| Lua e Marte | `Environment environment(Center, campo, grau)`: centro inercial no corpo com eixos ICRF; GRAIL GRGM1200A (Lua, eixos principais DE440) e MRO GMM-3 (Marte) baixados até o grau pedido (≤1200 e ≤120); rotação IAU de `pck00011` com ω analítico (Lua: referencial médio da Terra com librações, o dos mapas; o campo é avaliado nos eixos principais DE440, 67,8526″/78,6944″/0,2785″ adiante; Marte: IAU 2015); terceiros corpos Terra+Sol (Lua) e Sol por `plan94` (Marte); Marte com atmosfera média hidrostática sobre as temperaturas da NASA Glenn até 140 K perto de 50 km, isotérmica acima e vácuo acima de 150 km; Lua sem ar | série IAU da Lua a ≤155 m da libração do DE na superfície; Marte sem estações, poeira nem vento; alturas sobre elipsoide (esfera de 1737,4 km na Lua; 3396,19 × 3376,20 km em Marte), não sobre o areoide |
| Aquecimento | convectivo no ponto de estagnação, Sutton-Graves (k do ar e do CO2 marciano, `Stage::noseRadius`), carga térmica integrada no RK4; nós de proteção térmica (`Stage::thermal`, `physics/thermal`: o fluxo de estagnação vezes um fator e cos θ ao escoamento — Newtoniano modificado —, radiação ao céu e condução à estrutura, implícito em T⁴; evento `OVERHEAT` acima do limite) | sem radiação do choque (>10 km/s), sem ablação; nós sem condução entre si |
| Radiação solar | pressão de esfera (`Vehicle::sunArea`, `reflectivity`; 1361 W/m² a 1 au) com umbra e penumbra cônicas do corpo central | desligada por padrão; sem albedo nem infravermelho do planeta; atitude ignorada |
| Solo | pad com a base do conjunto no solo, preso girando com o corpo até empuxo > peso; solo a uma altura elipsoidal fixa (do relevo medido com `terrain_height`); pernas de pouso (`Stage::legs`) como molas-amortecedores com atrito de Coulomb regularizado (linear abaixo de 5 cm/s), força e momento em cada pé; sem pernas, toque detectado no CG e o veículo para | solo plano em torno do sítio; sem deformação do regolito nem rebote do corpo |

Sensores de navegação além do IMU (`physics/sensors`), reproduzíveis pela semente: GNSS (ruído branco horizontal, vertical
e de velocidade mais deriva de Gauss-Markov; só na Terra; limites de altura e velocidade, de fábrica um receptor espacial até
3000 km, com 18 km e 515 m/s um civil), altímetro radar (alcance inclinado pelo eixo de mira até o solo plano do sítio,
dentro do meio ângulo do feixe e do alcance; ruído máx(piso, fração do alcance)) e rastreador de estrelas (atitude com ruído
através e em torno do eixo de mira; nada a menos do ângulo de exclusão do Sol, dentro do disco do corpo central mais uma
margem, ou acima da taxa limite). Cada um mede no fim do passo que completa o seu período; `physics_test` confere os
desvios contra os ajustes (GNSS 1,53/2,96 m e 0,050 m/s para 1,5/3/0,05; radar 0,99 m a 100 m; rastreador 4,95″/42″).

Chaves por voo (`Environment`: `sunMoon`, `relativity`, `atmosphere`, `wind`, `solarPressure`, `turbulence`; na API, `PhysicsSettings`, e
o grau 0 da gravidade como massa pontual) removem cada modelo para estudos e validação.

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

Som (`tests/acoustics_test.cpp`): absorção contra a ISO 9613-2 Tabela 2 (10 °C, 70%, oitavas de 63 Hz a 8 kHz, ≤2%) e
a ISO 9613-1 Tabela 1 (−20 °C, 10%: 1,649 e 16,36 dB/km a 1 e 10 kHz; 1,65 e 16,4); o espectro e o lóbulo levam a potência
uma vez; nível global máximo com o veículo a 25° acima do horizonte (65° da pluma), 1,5 m sobre solo duro, contra as medições
da BYU: Starship 144,9/130,6/124,4 dB a 1/5/10 km (145,7/131,5/125,2), SLS 136,9 e 125,8 dB a 1,48 e 5,15 km (136 e 128),
pico do SLS no terço de 16 Hz (~20 Hz medidos); tempo retardado, Doppler de fonte que se afasta e silêncio no vácuo, através
do solo, antes da chegada e com o motor parado.

Apollo 11 (`tests/apollo_test.cpp`): o Saturn V AS-506 pelo seu programa de voo (`examples/saturn_v.hpp`) na
climatologia do Albert para 1969-07-16 13:32 UTC em 39A (NRLMSISE-00 e HWM14), contra o relatório de voo:

- S-IC do pad, com o programa de arfagem do Apollo 12 (o do AS-506 não foi publicado; eles eram ajustados aos
  ventos médios do mês): Mach 1 aos 65,8 s (voo 66,3), q máxima 35,5 kPa aos 82,0 s (35,2 aos 83,0), velocidade
  inercial 1.980,1 m/s no corte central (1.979,0) e 2.761,0 m/s no corte (2.764,1). A trajetória fica com a do
  Apollo 12, que voou esse programa: 68,8 km e 20,88° no corte (68,1 e 20,51); o Apollo 11, de julho, cortou a
  66,1 km e 19,11°. Com a US Standard Atmosphere a q máxima vinha 7 s cedo (ar tropical mais denso aos 13 km).
- S-II e S-IVB do estado reconstruído na separação (162,3 s), com uma guiagem de seno linear no lugar do IGM
  (altura e razão de subida nulas na inserção, desvio do plano de 32,521° anulado; nenhum parâmetro ajustado):
  corte do S-II a 187,2 km e 6.915,7 m/s (187,3 e 6.916,1), rumo 82,42° (82,39); corte do S-IVB aos 701,3 s
  (699,34) com 134.952 kg (135.182), órbita de 186,5 × 183,1 km (186,0 × 183,2) a 32,521°. No meio do S-II a
  trajetória fica 0,5° mais íngreme que a do IGM (176,2 km e 1,41° contra 180,2 e 0,90).

- Descida do Eagle (`apolloLunarModule`, guiagem de `examples/apollo_descent.hpp`: a quadrática de Klumpp do Luminary 99
  com os alvos P63/P64 da carga do LM-5, P65 a 3 ft/s; GRGM1200A grau 8), do estado reconstruído na ignição (15.050 m
  sobre o sítio, 1.696,2 m/s inerciais) contra o plano (Bennett, NASA TN D-6846, Tabela I) e o voo: recuperação do
  acelerador aos 390,6 s (plano 384, voo 386), portão alto aos 499,3 s a 2.394 m e −42,6 m/s (plano 506 s, 2.291 m,
  −44,2 m/s; voo 507 s, 2.173 m), portão baixo de 512 ft aos 604,6 s (606), toque aos 711,6 s (plano 714; o voo,
  pilotado à mão sobre o campo de rochas, 754,7 s), Δv 2.065,4 m/s (plano 6.775 ft/s = 2.065,0), 469 kg de
  propelente no toque (plano 957 lb = 434 kg; voo 349 kg), a 16 m do ponto mirado (navegação exata).

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
- `apolloLunarModule`: o Eagle (LM-5) na ignição da descida pelo relatório da missão Apollo 11: 15.151 kg, 8.128 kg
  no estágio de descida (7.023 kg sem eles: no toque, 7.327 kg menos 349 kg de sobra e 43 kg do RCS fecham), DPS de
  46.706 N nominais, acelerável de 10% a 65% e a 93,5% (os 43.670 N do Luminary 99) acima, velocidade de exaustão
  efetiva do voo (2.112 m/s medidos sobre o propelente queimado: 2.931 m/s), RCS ~3 kN m, pernas do `genericLander`.
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
- `saturnV`: o Saturn V do Apollo 11 (AS-506) pelo relatório de avaliação de voo (MPR-SAT-FE-69-9): massas
  medidas nos eventos (2.899.008 kg na liberação; 827.292 kg no corte do S-IC; 658.363 kg na ignição do S-II;
  213.888 kg no corte; 166.571 kg na ignição do S-IVB), S-IC de 40,11 MN no vácuo menos 56,94 m² × pressão
  (Fig. 5-3: 34,3 MN na decolagem e 40,1 MN antes do corte central; a subida do empuxo com a aceleração fica
  nessa área efetiva) e a vazão que queima o propelente até o corte aos 161,63 s; S-II a 423,2 s com a vazão
  que esgota os 436.451 kg aos 548,22 s (5,10 MN, nas razões medidas após o corte central e a mudança de
  mistura); S-IVB de 901.223 N a 428,7 s; coeficiente axial com jato do S-IC por Mach (Boeing D5-15481-1);
  interestágio (3.982 kg) e torre de escape (4.042 kg) largados como estágios; atitude por torque ideal.
- `genericLauncher`: lançador médio de dois estágios a querosene dimensionado por números
  públicos aproximados do Falcon 9 v1.2 (massas, empuxo, Isp, geometria); aerodinâmica de
  corpo esbelto com escoamento cruzado de Allen-Perkins. Aerodinamicamente instável: exige
  controle ativo. Não representa nenhum veículo real com precisão.
