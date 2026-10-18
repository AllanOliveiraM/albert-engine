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
| Atmosfera | US Standard Atmosphere 1976 até 86 km geométricos, co-rotativa, sem vento | vácuo acima de 86 km até a fase 2 (NRLMSIS) |
| Aerodinâmica | tabelas bilineares (Mach, ângulo); arrasto oposto ao ar relativo, sustentação no plano x-z, força lateral em y; amortecimento opcional | fora da tabela o valor é mantido |
| Propulsão | `F = F_vac·throttle − p_a·A_e`; `ṁ = F_vac·throttle/v_e`; cardã em arfagem e guinada | sem transiente de ignição |
| Massa | propriedades do conjunto lineares no propelente restante; estagiamento move o CG | inércia principal (sem produtos) |
| Rotação | `I ω̇ = M − ω×Iω − İω − ṁ l² ω⊥` (perda de inércia do propelente e amortecimento por jato) | — |
| Solo | pad com a base do conjunto no solo, preso girando com a Terra até empuxo > peso; toque a uma altura elipsoidal fixa, detectado no CG | sem terreno, contato nem atrito |

Integração RK4 com passo fixo (1 ms por padrão); `advance` divide o passo no instante
exato do fim do propelente e para ali (e também na liberação do pad e no toque), para que
estagiamentos programados sejam exatos. Em voo balístico acima da atmosfera (sem empuxo,
fora do pad, com margem de altura para o passo) o passo cresce até 1 s, limitado a 0,05 rad
de rotação por passo: uma volta em órbita fica a <1 mm do resultado com 10 ms e roda dezenas
de vezes mais rápido.
A mesma sequência de passos reproduz o estado bit a bit (`Flight::hash`); igualdade
entre plataformas e compiladores não é prometida.

Sensores ideais em `Flight::sample()`: força específica no corpo (o que um IMU mede:
zero em queda livre, +g no pad), taxas angulares, pressão estática, ar, Mach, pressão
dinâmica, α/β, ângulos de Euler em NED e estado verdadeiro (inercial, ECEF, geodésico).

## Validação (`tests/physics_test.cpp`)

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
- Pad (base no solo), liberação no empuxo, acelerômetro e pouso.
- O guiamento de exemplo leva o lançador genérico a uma órbita ~182 × 212 km.
- Coast de uma volta com passos de 1 s contra 10 ms: diferença <1 mm.

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
