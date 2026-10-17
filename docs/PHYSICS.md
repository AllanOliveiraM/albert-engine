# Física de voo

Núcleo sem renderização em `src/physics` (`albert_physics`, só depende de ERFA):
corpo rígido de seis graus de liberdade com massa variável, da superfície à órbita.
Estado e cálculo em `double`, SI, passo fixo; nada depende do FPS.

## Modelos

| Área | Modelo | Limite conhecido |
| --- | --- | --- |
| Tempo | TT em nanossegundos inteiros desde J2000; UTC por `eraDat` | antes de 1960 TAI−UTC = 0 |
| Referenciais | GCRF inercial; ITRF por IAU 2006/2000A (CIO) + ERA, matriz C2I a cada 60 s interpolada | movimento do polo ignorado (≤15 m); TT−UT1 fixo por voo |
| Gravidade | WGS 84; GM e J2–J4 do EGM2008 (tide-free) | sem harmônicos tesserais (EGM2008 20×20 vem depois) |
| Terceiros corpos | Sol (epv00) e Lua (moon98), aceleração de maré, 60 s interpolados | sem planetas; sem relatividade |
| Atmosfera | US Standard Atmosphere 1976 até 86 km geométricos, co-rotativa, sem vento | vácuo acima de 86 km até a fase 2 (NRLMSIS) |
| Aerodinâmica | tabelas bilineares (Mach, ângulo); arrasto oposto ao ar relativo, sustentação no plano x-z, força lateral em y; amortecimento opcional | fora da tabela o valor é mantido |
| Propulsão | `F = F_vac·throttle − p_a·A_e`; `ṁ = F_vac·throttle/v_e`; cardã em arfagem e guinada | sem transiente de ignição |
| Massa | propriedades do conjunto lineares no propelente restante; estagiamento move o CG | inércia principal (sem produtos) |
| Rotação | `I ω̇ = M − ω×Iω − İω − ṁ l² ω⊥` (perda de inércia do propelente e amortecimento por jato) | — |
| Solo | pad preso girando com a Terra até empuxo > peso; toque a uma altura elipsoidal fixa | sem terreno, contato nem atrito |

Integração RK4 com passo fixo (1 ms por padrão); `advance` divide o passo no instante
exato do fim do propelente e para ali, para que estagiamentos programados sejam exatos.
A mesma sequência de passos reproduz o estado bit a bit (`Flight::hash`); igualdade
entre plataformas e compiladores não é prometida.

Sensores ideais em `Flight::sample()`: força específica no corpo (o que um IMU mede:
zero em queda livre, +g no pad), taxas angulares, pressão estática, ar, Mach, pressão
dinâmica, α/β, ângulos de Euler em NED e estado verdadeiro (inercial, ECEF, geodésico).

## Validação (`tests/physics_test.cpp`)

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
- Pad, liberação no empuxo, acelerômetro e pouso.

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
