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
| Terceiros corpos | Sol (epv00) e Lua (moon98), aceleração de maré, 60 s interpolados | sem planetas; sem relatividade |
| Atmosfera | NOAA GFS 0,25° ao vivo (41 níveis isobáricos até 0,01 hPa ≈ 80 km: altura geopotencial, temperatura, vento, umidade específica), interpolado no tempo, na horizontal e na vertical; acima (transição de 8 km) e fora do recorte de ±6° (transição de 0,5°), NRLMSISE-00 até 1000 km com F10.7 e Ap da CelesTrak (observados ou previstos); sem índices, US Standard Atmosphere 1976 (vácuo acima de 86 km); co-rotativa | NRLMSIS 2.x excluído pela licença (uso acadêmico, sem modificação); sem ventos acima do GFS (HWM pendente) |
| Vento | do GFS no veículo; a velocidade do ar desconta vento e rotação da Terra | rajadas e turbulência não modeladas |
| Aerodinâmica | tabelas bilineares (Mach, ângulo); arrasto oposto ao ar relativo, sustentação no plano x-z, força lateral em y; amortecimento opcional | fora da tabela o valor é mantido |
| Propulsão | `F = F_vac·throttle − p_a·A_e`; `ṁ = F_vac·throttle/v_e`; cardã em arfagem e guinada | sem transiente de ignição |
| Massa | propriedades do conjunto lineares no propelente restante; estagiamento move o CG | inércia principal (sem produtos) |
| Rotação | `I ω̇ = M − ω×Iω − İω − ṁ l² ω⊥` (perda de inércia do propelente e amortecimento por jato) | — |
| Lua e Marte | `Environment environment(Center, campo, grau)`: centro inercial no corpo com eixos ICRF; GRAIL GRGM1200A (Lua, eixos principais DE440) e MRO GMM-3 (Marte) baixados até o grau pedido (≤1200 e ≤120); rotação IAU de `pck00011` com ω analítico (Lua: referencial médio da Terra com librações, o dos mapas; o campo é avaliado nos eixos principais DE440, 67,8526″/78,6944″/0,2785″ adiante; Marte: IAU 2015); terceiros corpos Terra+Sol (Lua) e Sol por `plan94` (Marte); Marte com a atmosfera média da NASA Glenn até ~49 km, isotérmica a 140 K acima e vácuo acima de 150 km; Lua sem ar | série IAU da Lua a ≤155 m da libração do DE na superfície; Marte sem estações, poeira nem vento; alturas sobre elipsoide (esfera de 1737,4 km na Lua; 3396,19 × 3376,20 km em Marte), não sobre o areoide |
| Solo | pad com a base do conjunto no solo, preso girando com a Terra até empuxo > peso; toque a uma altura elipsoidal fixa, detectado no CG | sem terreno, contato nem atrito |

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
  isotérmica, densidade decrescente até o vácuo.
- Órbitas circulares de 100 km na Lua e 300 km em Marte: uma volta com passos de 1 s a <1 µm
  de 0,1 s.

## API

`FlightService` (gRPC, lockstep) expõe o voo a software externo e espelha o veículo na
cena; ver [RPC.md](RPC.md#voo-físico-em-lockstep).

## Veículos

- `nescSphere`, `nescTwoStage`: modelos DAVE-ML da NESC convertidos para SI. O foguete da
  NESC é conceitual ("provavelmente inviável") e sem guiamento.
- `genericLauncher`: lançador médio de dois estágios a querosene dimensionado por números
  públicos aproximados do Falcon 9 v1.2 (massas, empuxo, Isp, geometria); aerodinâmica de
  corpo esbelto com escoamento cruzado de Allen-Perkins. Aerodinamicamente instável: exige
  controle ativo. Não representa nenhum veículo real com precisão.
