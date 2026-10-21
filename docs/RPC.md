# Controle externo por gRPC

O servidor é opcional e funciona dentro do Albert. A interface continua
operando sem cliente conectado. O contrato está em
[engine.proto](../proto/albert/v1/engine.proto); o exemplo completo em
[rpc_client.cpp](../examples/rpc_client.cpp).

## Executar

Compile normalmente. O CMake baixa as versões fixadas do gRPC/Protobuf e gera
os códigos necessários; não é preciso instalar um servidor separado ou `protoc`.
A primeira compilação demora mais por incluir essas bibliotecas.

Linux, em um terminal:

```sh
./build/linux/bin/AlbertEngine --rpc
```

Em outro terminal:

```sh
./build/linux/src/rpc/albert_demo_client
```

Windows, em dois terminais:

```powershell
.\build\windows\bin\Release\AlbertEngine.exe --rpc
.\build\windows\src\rpc\Release\albert_demo_client.exe
```

O exemplo cria um cilindro com dimensões de 9 × 120 × 9 m, foca a câmera e
transmite posição/atitude por aproximadamente dez segundos. É um voo sintético,
identificado como simulado; não representa telemetria de uma missão real.
Arraste e use o zoom durante o acompanhamento. Ao terminar, a última posição
permanece visível. Remova o exemplo antes de repetir:

```sh
./build/linux/src/rpc/albert_demo_client 127.0.0.1:50051 --remove
```

Sem `--rpc`, nenhuma porta é aberta. O servidor aceita somente conexões em
`127.0.0.1`. Use `--rpc-port=50052` para escolher outra porta e passe o mesmo
endereço ao cliente. Acesso remoto, autenticação e TLS configurável não fazem
parte deste primeiro corte. `--demo` cria um objeto sintético sem usar gRPC.

Para compilar somente o programa visual, configure `-DALBERT_RPC=OFF`.

## Operações disponíveis

| Serviço | Operações |
| --- | --- |
| `SceneService` | `GetScene`, `GetCapabilities`, `CreateObject`, `RemoveObject`, `SetVisibility`, `PushStates`, `StreamStates` |
| `CameraService` | `Focus`, `Orbit`, `GetCamera` |
| `TimeService` | `GetTime`, `SetTime` |
| `LayerService` | `CreateLayer`, `ListLayers`, `SetVisibility`, `RemoveLayer` |
| `AssetService` | `RegisterModel`, `ListModels`, `RemoveModel` |
| `CatalogService` | `ListObjects`, `ImportObject`, `GetJob`, `GetJobs`, `RetryJob` |

`GetCapabilities` informa os formatos e limites realmente suportados. IDs dos
corpos com mapas próprios: `earth`, `moon`, `sun`, `mercury`, `venus`, `mars`, `jupiter`, `saturn`, `uranus`, `neptune`. Eles não podem ser removidos nem ter
seus estados substituídos por um cliente. Os 33 corpos do catálogo offline também são protegidos. Os alvos especiais `@earth-moon`,
`@inner-system` e `@system` enquadram as vistas de contexto.

Objetos externos aceitam esfera, caixa, cilindro e modelo GLB registrado, com dimensões físicas e cor
RGB. O eixo do cilindro é o Y local; a esfera admite dimensões diferentes por
eixo. Nomes não precisam ser únicos; IDs precisam. Ao remover o objeto focado,
a câmera volta à Terra. Ocultar um objeto preserva seu estado e o acompanhamento.
Imagens externas e trajetórias de objetos externos desenhadas serão acrescentadas
nos próximos cortes. O primeiro suporte a modelos está descrito abaixo.

Ao terminar um `Focus` em um corpo, a câmera inicia uma órbita lenta de
apresentação. Arraste/zoom local ou um comando `Orbit` interrompem esse movimento;
`Orbit` com ângulos zero e `distance_scale=1` mantém o enquadramento e o deixa
parado. Vistas de conjunto não giram automaticamente. Um novo `Focus` ou uma
restauração de zoom inicia a apresentação novamente ao concluir a aproximação.

## Catálogo interno e importação automática

A interface pesquisa apenas os corpos incluídos. Importações adicionais são feitas
por gRPC; o Albert consulta a fonte,
cria o objeto e renova suas posições sem exigir consultas externas pelo cliente.

```sh
./build/linux/src/rpc/albert_catalog_client 127.0.0.1:50051
./build/linux/src/rpc/albert_catalog_client 127.0.0.1:50051 himalia
./build/linux/src/rpc/albert_catalog_client 127.0.0.1:50051 --jobs
./build/linux/src/rpc/albert_catalog_client 127.0.0.1:50051 --remove himalia
```

No Windows, o cliente fica em
`build\windows\src\rpc\Release\albert_catalog_client.exe`.
O catálogo de importação da API contém 71 corpos: Sol, oito planetas, Lua, Plutão e 60 luas adicionais. `ListObjects.query` filtra nomes/IDs. Importações
sem assets embarcados usam **esferas simplificadas**, com raio de referência;
não prometem imagens, anéis, relevo ou orientação detalhada. Um corpo embarcado
já existente pode ser focado diretamente.

`ImportObject` aceita ID do catálogo, ID opcional para a cena, cor opcional e
pedido de foco. Retorna uma operação assíncrona; acompanhe `GetJob` até `TRACKING`,
`FAILED` ou `REMOVED`. `APPLYING` ainda não significa que o objeto apareceu;
`applied_revision` identifica a revisão efetivamente aplicada. O foco acompanha
o alvo sem impedir órbita/zoom manuais. `GetJobs` e `RetryJob` também atendem à UI.

As posições são vetores geométricos ICRF do NASA/JPL Horizons, convertidos de
km/km/s e TDB para metros, m/s e UTC. A consulta cobre aproximadamente 30 minutos
adiante, com amostras de cinco minutos e renovação a cada cinco minutos.
`fetched_utc_unix_seconds` e `valid_until_utc_unix_seconds` informam a origem
temporal dos dados. A esfera não constitui um modelo físico detalhado do corpo.

`Object.provider_managed` identifica estados sob controle do provedor: clientes
podem focar, ocultar e remover essas importações, mas `PushStates`/`StreamStates`
são rejeitados para elas. Crie outro ID externo para transmitir uma trajetória
própria. Remover interrompe o acompanhamento automático. Falhas posteriores
ficam em `DEGRADED`, preservam a última curva e retentam com espera de 15–300 s;
fora da cobertura, o último estado é mantido e marcado desatualizado.

O serviço limita a 16 importações na fila, 128 acompanhamentos e 256 operações
no histórico. O adaptador Horizons é compartilhado com o render, serializa as
consultas, mantém até 32 tabelas em memória e valida cobertura, ordem e valores.
Cache de cinco minutos para tabelas curtas e 24 horas para órbitas; respostas
HTTP têm limite de 4 MiB e 90 s, com cancelamento. Nenhuma consulta ocorre por quadro.
As respostas validadas são salvas em disco e reutilizadas entre execuções;
na inicialização, tabelas com até 24 horas podem antecipar a abertura somente
se cobrirem o instante atual. A cena renova os dados em segundo plano. As
posições incluídas mantêm amostras a cada cinco minutos para o próximo dia;
isso não aumenta o intervalo de atualização de cinco minutos. O cache de
órbitas tem validade normal de 24 horas e o de posições/consultas SBDB,
cinco minutos. Cada diretório de provedor tem ainda um teto de 128 respostas
e 64 MiB, dentro do limite de cache configurado pelo usuário.
`SearchObjects({query:"Apophis"})` consulta a NASA/JPL SBDB (API 1.3).
A pesquisa é explícita, por nome/designação; aceita até 40 caracteres ASCII,
incluindo `*` para padrões, e retorna até 50 resultados. `ListObjects` continua
local: são 71 corpos, incluindo 60 luas. A busca online foi retirada da UI; estes serviços permanecem disponíveis aos integradores.

Use o `id` retornado por `SearchObjects` em `ImportObject`; IDs `sbdb.` codificam
a designação e são opacos para clientes. O worker resolve o SPK e busca seus
vetores pelo Horizons usando `DES=<SPK>;`, sem confundir SPK com número de registro.
A origem continua dentro do Albert. `reference_radius_meters=0` significa diâmetro
indisponível: a importação usa raio visual de 1 km, declarado em `Object.source`.
Tamanhos conhecidos vêm do diâmetro SBDB em km; esferas não representam a forma real.

SBDB e Horizons compartilham uma fila de acesso à rede. SBDB limita respostas a
2 MiB/20 s, conserva até 32 pesquisas por cinco minutos, verifica a versão e aceita cancelamento. Consultas não rodam na thread
gráfica. Clientes devem configurar prazo de pelo menos 25 s e evitar pesquisas
por tecla; ampliar resultados exige refinar a consulta. Outros provedores seguem
no roadmap: não há agregação de centenas de fontes nesta entrega.

```sh
./build/linux/src/rpc/albert_catalog_client 127.0.0.1:50051 --search Apophis
# Copie o ID retornado para importar e acompanhar:
./build/linux/src/rpc/albert_catalog_client 127.0.0.1:50051 sbdb.3939393432
```

Referência: [NASA/JPL SBDB API](https://ssd-api.jpl.nasa.gov/doc/sbdb.html).

## Modelos externos GLB

O primeiro corte aceita **GLB 2 estático, opaco e sem texturas**: triângulos,
`POSITION`/`NORMAL` float32, índices uint8/16/32, hierarquia local com TRS ou
matrizes afins e materiais PBR de cor sólida. Normais ausentes usam faces planas.
A hierarquia é aplicada à geometria no registro; animação de peças, skins,
morph targets, compressão/extensões, transparência, emissivos, imagens e URIs
externas são rejeitados explicitamente. Não é um importador geral de glTF.

`AssetService.RegisterModel` recebe ID, proveniência `source` e bytes GLB.
A decodificação/validação acontece na thread do transporte; a thread da cena
registra o recurso imutável. O arquivo não é salvo nem acessa disco/rede.
`ListModels` retorna metadados do último quadro. Há limite de **32 assets,
768 KiB por GLB, 16.384 vértices, 32.768 triângulos, 64 primitivas e 128 nós**;
instâncias de uma malha no arquivo contam no orçamento decodificado.
JSON limita-se a 128 KiB, profundidade 64; hierarquia limita-se a 32 níveis.

Em `CreateObject.geometry`, use `shape=MODEL`, `model_id` e opcionalmente
`meters_per_model_unit` (padrão 1). O modelo conserva sua origem, eixos locais,
cores e proporções; `size_meters`/`color_rgb` destinam-se às primitivas. O Albert
calcula raio e distâncias da câmera a partir da geometria e da escala física.
A atitude do objeto gira os eixos do modelo para ICRF, como nos demais objetos.

Crie e carregue uma demonstração pequena:

```sh
python3 scripts/create-demo-model.py
./build/linux/src/rpc/albert_model_client build/demo-vehicle.glb 127.0.0.1:50051
```

No Windows, use `python` e
`build\windows\src\rpc\Release\albert_model_client.exe`.
O gerador produz um veículo **ilustrativo**, sem telemetria ou modelo de missão.
O cliente registra `example-model`, cria `example-vehicle` em posição sintética
e pede foco. Órbita e zoom manuais continuam disponíveis. Atualize o veículo
pelos mesmos `PushStates`/`StreamStates`; isso não exige reenviar o GLB.

```sh
./build/linux/src/rpc/albert_model_client --remove 127.0.0.1:50051
```

Remova objetos dependentes antes de `RemoveModel`. A geometria/material GPU é
compartilhada entre instâncias e liberada quando o asset removido deixa o render.
Os assets externos duram apenas a sessão. IDs podem ser reutilizados depois da
remoção; o render reconhece a nova instância e substitui os recursos anteriores.
Referência de formato: [especificação Khronos glTF 2.0](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html).

## Camadas vetoriais — pontos e linhas

`LayerService.CreateLayer` cria `POINTS` ou `POLYLINE` com ID, nome, fonte,
cor RGB e largura em pixels lógicos. Os pontos são marcadores circulares;
as linhas seguem a ordem fornecida. O objeto continua sendo gerenciado pela
cena. `ListLayers` consulta metadados; `SetVisibility` e `RemoveLayer` aplicam
alterações na thread gráfica e conservam transições suaves. A UI oferece os
mesmos controles em **☰ → Cena → Camadas**, incluindo a fonte.

Escolha um referencial explicitamente:

| `frame` | Dados enviados | Comportamento |
| --- | --- | --- |
| `GEOCENTRIC_ICRF` | `positions_meters`, sem `anchor_id` | Pontos espaciais em metros/double; a origem gráfica acompanha a câmera |
| `OBJECT_LOCAL` | `positions_meters` e `anchor_id` | Metros nos eixos físicos locais do objeto; acompanha posição/atitude e visibilidade, sem herdar a escala visual da malha |
| `EARTH_SURFACE` | `geographic_points` em longitude/latitude, graus | Terra WGS84; acompanha rotação e relevo global/regional exibido. `anchor_id` pode ser omitido ou `earth` |

Para a Terra, longitude é positiva para leste, −180° a 180°; latitude geodética,
−90° a 90°. `clearance_meters` define o afastamento sobre o terreno exibido,
padrão 100 m, de 0 a 1.000.000 m. Não representa uma altitude observada. Linhas
são subdivididas a cada no máximo 0,05° em latitude/longitude, usando a passagem
curta pela linha de data. Essa interpolação cartográfica não calcula uma rota
geodésica nem uma trajetória orbital. Forneça os vértices adequados ao dado real.

Os eixos de `OBJECT_LOCAL` correspondem aos usados na atitude do objeto. Nos
corpos embarcados, são os eixos físicos fixos ao corpo: X no meridiano zero,
Y a 90° leste, Z no polo norte. A superfície geográfica neste corte suporta
somente a Terra. Modelos externos conservam seus próprios eixos locais.

Limites: **64 camadas**, 2.048 pontos de entrada por chamada, até 16.384 pontos
após subdivisão por camada e 32.768 no conjunto da cena. A preparação geográfica
ocorre antes de enfileirar o comando; snapshots não copiam a geometria completa.
Buffers ancorados são reutilizados entre quadros. Pontos/linhas participam do
teste de profundidade; não se tornam visíveis através de corpos opacos.
Remover o objeto de referência remove também suas camadas. IDs de camadas
formam um espaço separado dos IDs de objetos. Alterações geométricas requerem
remover e recriar a camada neste primeiro corte.

```sh
./build/linux/src/rpc/albert_layers_client 127.0.0.1:50051
./build/linux/src/rpc/albert_layers_client 127.0.0.1:50051 --remove
```

No Windows, use `build\windows\src\rpc\Release\albert_layers_client.exe`.
O exemplo marca três localizações no Brasil e desenha uma linha ilustrativa;
não representa uma rota real. Polígonos, rótulos, importação GeoJSON, ícones
personalizados e camadas raster ainda são próximas entregas.

## Voo físico em lockstep

`FlightService` controla um veículo de seis graus de liberdade integrado pelo núcleo de
física do Albert ([PHYSICS.md](PHYSICS.md)); o software externo faz o papel do
computador de voo. `CreateFlight` põe o veículo preso no pad, com a base no solo, (`GENERIC_LAUNCHER`,
`GENERIC_LANDER`, `GENERIC_LUNAR_ASCENT`, `GENERIC_MARS_ASCENT`, `SATURN_V`, `NESC_TWO_STAGE` ou `NESC_SPHERE`), no sítio, rumo e instante pedidos (padrão: o
instante exibido), com passo fixo de 1 ms (de 10 µs a 100 ms). Em voo balístico acima
de 86 km o Albert usa passos internos de até `coast_step_seconds` (padrão 1 s; 0 desliga),
então chamadas longas propagam órbitas rapidamente; os ticks continuam exatos. A gravidade usa o
EGM2008 até `gravity_degree` (padrão 20; de 2 a 70). `weather` escolhe o ar: `WEATHER_AUTOMATIC` (padrão) usa o
NOAA GFS ao vivo quando o Albert segue o relógio real e o instante está a até 2 h de agora
(recorte de ±6° em torno do sítio, ~1,5 MB por voo, guardado por 48 h), senão o NRLMSISE-00
(ou a US Standard Atmosphere 1976 sem índices solares); `LIVE_GFS` exige o GFS e falha com `UNAVAILABLE` sem ele;
`STANDARD_ATMOSPHERE` força a de 1976. Fora desse caso, acima do GFS e sem ele, o ar vem do
NRLMSISE-00 com os índices solares e geomagnéticos da CelesTrak para o instante do voo e com os ventos do HWM14
(fonte `NRLMSISE-00/HWM14`; logs gravados antes dele não se repetem: o replay recusa a fonte antiga). A amostra informa a fonte (`atmosphere`) e o vento
no veículo, e o painel **Voo** no canto inferior direito mostra estado, vento e fonte.

`terrain_height` põe a base no relevo medido: a altura do sítio vira Copernicus DEM 30 m mais o
geoide EGM2008 (altura elipsoidal), resolvida na criação e gravada no log de replay.

A amostra traz também o aquecimento convectivo no ponto de estagnação (Sutton-Graves, raio de nariz
do estágio) e a carga térmica acumulada.

O `GENERIC_LANDER` tem pernas com molas e atrito: `leg_contact` (bit `contact` no plano binário)
diz se alguma perna toca o solo, e `TOUCHDOWN`/`LIFTOFF` marcam o primeiro pé no chão e o último a
sair (quiques incluídos); ele pode reacender e saltar. Veículos sem pernas param no toque (`landed`).

`body` escolhe o corpo central: `EARTH` (padrão), `MOON` ou `MARS`. Na Lua e em Marte a gravidade
é GRAIL GRGM1200A (grau 2 a 600) ou MRO GMM-3 (2 a 120), padrão 100, lida do PDS só até o grau
pedido (≈630 kB no grau 100) na primeira vez e guardada; o log de replay leva o trecho usado. O
sítio está no referencial médio da Terra da Lua (esfera de 1737,4 km) ou no IAU 2015 de Marte
(longitude leste, latitude normal ao elipsoide de 3396,19 × 3376,20 km), altura de −10 a 25 km.
Marte tem uma atmosfera média hidrostática; a Lua, vácuo. `terrain_height` usa o LRO LOLA na Lua
e o MGS MOLA em Marte (postes de ~237 m, Marte até ±88°), lidos por faixas de bytes; `LIVE_GFS`
só existe na Terra. Posições e velocidades da amostra (inercial e fixo) são relativas ao corpo
central; na cena o veículo é desenhado sobre a Lua ou Marte da cena (Horizons, levado ao instante
pelo movimento do ERFA).

`imu` escolhe os erros do IMU e do barômetro (`IDEAL`, `TACTICAL`, `INDUSTRIAL`) e a semente do
ruído. A amostra traz, além dos valores ideais, os medidos sobre o último `Step`: força específica
e taxa médias, incrementos de velocidade e ângulo e pressão estática; com `IDEAL` coincidem com os
ideais. Os índices solares do NRLMSISE-00 acompanham o instante do voo. Cada `Step` aplica os
comandos — todos ou nenhum — e avança `ticks` passos (até 600 mil; uma chamada cancelada ou
vencida não altera o voo); a resposta traz a
amostra do novo tick. Assim, comandos do tick *k* produzem os sensores do tick *k+1*,
e a mesma sequência de comandos e passos reproduz o mesmo `state_hash`.

| Comando | Efeito |
| --- | --- |
| `ignite`, `cutoff` | Liga/desliga os motores do estágio ativo |
| `separate` | Solta o estágio ativo; o CG passa ao conjunto superior |
| `release` | Solta as garras do pad (também ocorre sozinho quando o empuxo supera o peso) |
| `throttle` | 0–1, limitado ao mínimo de cada motor |
| `gimbal` | Cardã em arfagem/guinada (rad), limitado ao curso de cada motor e alcançado na taxa dos atuadores |
| `fail` | O motor falha: apaga e não reacende (exige `engine`) |
| `torque_newton_meters` | Torque de atitude nos eixos do corpo: dos propulsores de RCS do estágio, se houver (ciclos alocados, consumo); senão ideal, limitado por eixo |
| `thruster_duty` | Ciclo de trabalho (0–1) de um propulsor de RCS, até o próximo comando de torque |
| `surface_radians` | Deflexão comandada de uma superfície de controle, limitada e alcançada na taxa do atuador |
| `brake` | Freio das rodas (0–1) |
| `steer_radians` | Direção de uma roda, limitada ao seu curso |

**Vários voos.** Cada voo tem um `flight_id` (letras, dígitos, `_` e `-`, até 32; vazio: `flight`) em `CreateFlight`,
`Step`, `GetFlight`, `EndFlight`, `ExportFlight`, `StartRealtime` e `StopRealtime`; criar um id existente o substitui,
`ListFlights` lista os vivos. Cada um tem a sua física, sensores, porta do plano de dados, tempo real e log, e aparece na
cena como `flight` (o padrão) ou `flight.<id>`. O relógio simulado segue o voo publicado por último; o som, o último na Terra.

**Separação em outro voo.** O comando `spawn` separa o estágio ativo e o faz voar como o voo `flight_id` (outro id): com a
sua própria definição (o estágio 0 é a parte que caiu), o nariz a `nose_offset_meters` atrás do nariz do veículo, a partir
do estado do voo no instante (velocidade mais ω × r, mesma atitude e taxas), com o propelente que restava (até o do seu
estágio), o mesmo ar, gravidade, chaves físicas e sensores (com sementes novas). Ele começa no instante do comando e passa
a andar por conta própria — o booster que volta, a nave que segue —; o log dele se reproduz sozinho e o do pai recria o
filho no replay.

**Colisões e destruição.** `VehicleDefinition.crash_speed_meters_per_second` faz do casco um sólido que bate no chão e nas
estruturas e se quebra acima dessa velocidade; `ThermalNodeDefinition.failure_kelvin` quebra o veículo quando o nó passa
dela. A amostra traz `breakup` (causa, instante, velocidade) e o evento `BREAKUP`; quebrado, os motores apagam, os comandos não
fazem nada e os pedaços caem até o chão. A cena mostra a bola de fogo, a fumaça e os destroços. Uma estrutura com
`drawn: false` não é desenhada como barra (o cenário a mostra).

**Propelente como nos tanques.** `StageDefinition.boiloff_per_day` (a fração que evapora por dia), `residual_kilograms` (o
que os motores não tiram), `thruster_gas_kilograms` (o gás próprio dos propulsores, na amostra
`thruster_gas_kilograms`) e `EngineDefinition.start_kilograms` (o propelante de cada partida).

**Marcos da missão.** `StepRequest.milestones` dá ao voo eventos da missão (nome de até 40 caracteres, instante UTC ou o
início do passo, previsto ou acontecido; o acontecido substitui o previsto do mesmo nome). A cena os mostra, com os de todos os
voos, na linha do tempo dos painéis de transmissão, junto do relógio T+ desde o início do voo principal.

**Água e estruturas.** `water` faz da superfície do sítio água do mar (o veículo flutua, é freado pela água e nunca "pousa":
amerissagem); `structures` põe segmentos fixos ao chão, em metros leste-norte-cima do sítio, com raio, mola, amortecedor e
atrito — os braços de uma torre de captura —, tocados pelos `hard_points_meters` dos estágios; a amostra traz a força de
contato. Com `hinge_enu_meters`, a estrutura gira em torno de `hinge_axis` (padrão: para cima) até o ângulo de
`FlightCommand.structure_radians` (`index`: a estrutura; 0: todas as articuladas), dentro de `min_radians`/`max_radians` e na
taxa `swing_rate_radians_per_second`, a partir de `start_radians`; a amostra traz os ângulos (`structure_radians`). Cada voo
tem sua cópia das estruturas (um voo gerado herda os ângulos do pai no instante): comande todos os que devem ver o mesmo.

**Aparência e cenário.** `StageDefinition.appearance` diz como o estágio se vê: o corpo em troncos de cone (`BodySection`,
com um arco de ladrilhos pretos), peças em dobradiças que giram com uma superfície (`HingedPart`: flaps, grid fins) e os
pinos; a cena desenha também os sinos dos motores com o cardã, as plumas (estreitas no ar denso, um leque azulado no vácuo,
pela pressão) e os jatos do RCS pelo ciclo de trabalho. `scenery` (`SceneryPart`) é o cenário do sítio, só desenhado:
caixas, tanques (cilindros, ocos ou não) e treliças, em metros leste-norte-cima, com rumo e inclinação; cada peça pode girar
com uma estrutura articulada (`structure`). A cena mostra o cenário e as estruturas (barras de aço) como o objeto `site`,
preso ao chão: seu lugar vem da rotação da Terra a cada quadro (o objeto ancorado do núcleo), não de amostras.

**Começar em voo.** `start` (`StartState`) cria o voo já no ar em vez do pad: sobre o corpo central (`local`: posição
geodésica, velocidade em relação ao solo em NED, rumo, arfagem e rolagem, taxas do corpo) ou no referencial inercial do voo
(`inertial`: posição a partir do centro do corpo, velocidade, atitude corpo→inercial, taxas), no estágio e com o propelente
escolhidos; o `site` passa a dar só a altura do solo e o lugar do tempo. Serve para começar direto numa reentrada ou numa órbita.

**Modelos físicos por voo.** `physics` (`PhysicsSettings`) liga ou desliga as marés dos terceiros corpos, a relatividade,
a atmosfera (desligada: vácuo), o vento e a pressão de radiação solar, e escolhe a turbulência de Dryden (intensidade e
semente); campos não definidos vêm dos padrões do Albert (a
página "Fisica" do menu; tudo ligado de fábrica) e o voo grava os resolvidos (amostra e log: o replay não depende dos
padrões). `gravity_degree: 0` faz do corpo central uma massa pontual. A página mostra também os modelos do voo atual.

**Sensores de navegação.** `sensors` (`SensorSettings`) liga GNSS, altímetro radar e rastreador de estrelas, cada um com
ruídos, taxa, limites e eixo de mira configuráveis (padrões no proto) e uma semente; a amostra traz a última medida de
cada um (`valid` falso quando não havia medida possível, como o GNSS fora da Terra ou o rastreador olhando o Sol), com o
instante em segundos de voo. Medem no fim do passo que completa o período (passos longos medem uma vez).

**Veículos definidos pela API.** `vehicle: CUSTOM` com `definition` (`VehicleDefinition`) voa qualquer veículo: estágios
na ordem de queima, cada um descrevendo o conjunto inteiro enquanto é o mais baixo (massas cheio e vazio, CG, inércias
principais, comprimento), seus motores (`EngineDefinition`: posição do bocal, direção, empuxo no vácuo, velocidade de
exaustão, área de saída, aceleração mínima, curso e taxa do cardã), aerodinâmica (tabelas Mach × ângulo de sustentação,
arrasto, força lateral e momentos, amortecimentos), rodas (`WheelDefinition`: ponto de contato, mola e amortecedor, rolamento, frenagem, curva, curso da direção), motores a
ar (`EngineDefinition.air_breathing` com tabelas de empuxo e consumo por Mach × altitude e o tempo do carretel),
nós de proteção térmica (`ThermalNodeDefinition`: posição, normal, área, capacidade térmica, emissividade, condução à
estrutura, fator de aquecimento e limite; a amostra traz temperatura e fluxo de cada um e o evento `OVERHEAT`),
superfícies de controle (`SurfaceDefinition`: limite, taxa,
incrementos em tabelas Mach × ângulo × deflexão e, opcionais, `position_meters`: as forças da superfície agem ali, com o
momento tirado de lá; `normal`: a sustentação age nessa direção do corpo, a força normal de uma grid fin), propulsores de RCS (`ThrusterDefinition`), pernas e o torque ideal de
atitude; posições em metros atrás do nariz.
`GetVehicle` devolve cada preset como definição, para editar: voada como `CUSTOM` sem mudanças, dá o mesmo `state_hash` do
preset (`rpc_test`). O mapeamento JSON do protobuf serve de formato de arquivo: `albert_flight_client --export-vehicle=launcher`
imprime o lançador em JSON e `--definition=arquivo.json` voa o arquivo do pad do Cabo com a guiagem do lançador (o mesmo
lançador com nove motores num anel de 1,4 m, cada um com 1/9 do empuxo e da área, chegou a 181 × 210 km).

`index` em cada comando escolhe o motor de `ignite`, `cutoff`, `throttle`, `gimbal` e `fail`, o propulsor de
`thruster_duty` e a superfície de `surface_radians`: 0 para todos os do estágio, n para o n-ésimo (a ordem de
`FlightSample.engines` — ligado, falho, aceleração, cardã movido e comandado, empuxo —, `thruster_duty` e `surfaces`);
nos demais comandos deve ser 0. A amostra traz também a força e o momento dos propulsores.

A amostra traz sensores ideais no centro de massa (força específica do IMU — zero em
queda livre, +g no pad —, taxa angular inercial, pressão estática e temperatura do ar),
a verdade em GCRF, ECEF, geodésica e NED, dados de ar (Mach, pressão dinâmica, α, β),
massa, propelente, empuxo e eventos desde o passo anterior (`LIFTOFF`, `DEPLETED`,
`TOUCHDOWN`). Eixos do corpo: x à frente, y à direita, z para baixo.

Enquanto o voo existe, a cena mostra o veículo como o objeto `flight` (cilindro com o
comprimento do conjunto atual) e o relógio simulado acompanha o tempo de voo, pausado
entre passos; ao voltar ao horário ao vivo, o voo continua, mas a cena deixa de
segui-lo. `EndFlight` remove o veículo.

**Plano de dados binário.** Para laços de controle de alta taxa, `data_port` na amostra é uma
porta TCP local (127.0.0.1) com quadros fixos little-endian definidos em
[flight_wire.hpp](../src/rpc/flight_wire.hpp): um pedido de 64 bytes (bits de comando — falha, corte,
separação, liberação, ignição, aceleração, cardã, torque, aplicados nessa ordem e de forma
atômica —, `ticks` e `engine`, o motor dos comandos de motor: 0 para todos) e uma resposta de 376 bytes (IMU medido, pressão, estado verdadeiro, atitude
em GCRF e NED, dados de ar, massa, eventos e hash). O mesmo núcleo do `Step` atende os dois
caminhos; o gRPC fica para criar, consultar e encerrar. Um cliente por vez; `wire::Connection`
em [wire.hpp](../src/rpc/wire.hpp) é um cliente pronto. Medido: ~30 µs por troca de um tick de
1 ms no teste e, no aplicativo com clima ao vivo, 600 mil trocas a 1 kHz (10 min de voo) em 46 s
sem perda (`albert_flight_client --wire`).

**Tempo real.** `StartRealtime` passa a avançar o voo pelo relógio de parede (`rate` vezes o
tempo real, até 100) e a empurrar pelo plano de dados uma amostra a cada `sample_every_ticks`,
descrevendo o voo `sensor_delay_ticks` antes. Quadros de comando (sem resposta; `ticks` ignorado)
valem `actuator_delay_ticks` depois de chegarem. Sem comandos por `command_timeout_seconds`, a
política `missing` age uma vez: `HOLD` mantém, `NEUTRAL` zera cardã e torque, `CUTOFF` desliga o
motor. `Step` é recusado enquanto isso; `StopRealtime` informa ticks, ticks atrasados e o maior
atraso. Os passos entram no log de replay e o voo se reproduz exatamente.

**Replay.** `ExportFlight` devolve, em pedaços de até 1 MiB, um `FlightLog` com o pedido de
criação (instante resolvido, semente do IMU), os dados de ar usados (GRIB2 das duas horas do GFS e
os dias de clima espacial) e cada passo com seus comandos na ordem em que chegaram, pelo gRPC ou
pelo plano de dados. `ReplayFlight` recria o voo só com esse conteúdo, sem rede, refaz os passos e
responde `DATA_LOSS` se o hash final divergir; um log cujo ar não pode ser reconstruído é
recusado com `FAILED_PRECONDITION`. A mesma sequência de passos é necessária: agrupar ticks de
outra forma muda os sorteios do IMU e a divisão dos passos.

O exemplo [flight_client.cpp](../examples/flight_client.cpp) leva o lançador genérico
de Kennedy (LC-39A) à órbita com a lógica de [gravity_turn.hpp](../examples/gravity_turn.hpp):
subida vertical, pitch kick, gravity turn sem incidência, controle da taxa vertical no
segundo estágio e corte quando o perigeu passa de 180 km (órbita ~182 × 212 km em 421 s);
depois propaga uma volta inteira em nove chamadas (~0,1 s de cálculo).

```sh
./build/linux/bin/AlbertEngine --rpc
./build/linux/src/rpc/albert_flight_client            # tempo real
./build/linux/src/rpc/albert_flight_client --fast     # o mais rápido possível
./build/linux/src/rpc/albert_flight_client --body=moon   # Tranquilidade → 18 × 85 km (Apollo 11)
./build/linux/src/rpc/albert_flight_client --body=mars   # Jezero → ~200 × 250 km, em duas queimas
./build/linux/src/rpc/albert_flight_client --apollo11    # Saturn V AS-506 de 39A em 1969-07-16 → ~186 × 183 km
./build/linux/src/rpc/albert_flight_client --starship    # Starship de Starbase → órbita; booster e, ~22 h depois, a nave capturados na torre
```

O `SATURN_V` (Apollo 11, AS-506) tem cinco estágios: S-IC; S-II com o interestágio de trás e a torre de
escape; S-II com a torre; S-II; S-IVB. `separate` larga cada um (o interestágio aos 192,3 s e a torre aos
197,9 s, reacendendo o S-II no mesmo passo: mande `separate` antes de `ignite`); `throttle` faz o corte
do motor central (0,8 no S-IC; 0,7961 no S-II) e a mudança de mistura do S-II (0,5996); atitude por torque
ideal até 1e10 N m. O `--apollo11` voa o programa de [saturn_v.hpp](../examples/saturn_v.hpp) com os
eventos do instrument unit e corta o S-IVB quando o semieixo maior chega ao da órbita de estacionamento.

O `--starship` ([starship_flight.hpp](../examples/starship_flight.hpp)) usa só peças genéricas da API, sem preset no
núcleo.

**Veículos e base**
- **Veículos:** a pilha e o Super Heavy são `CUSTOM` com `VehicleDefinition` (de [starship.hpp](../examples/starship.hpp)),
  com os números públicos da V3 (booster de 72,3 m, nave de 52,1 m, Raptor 3 de 280 tf):
  - 33 motores, dos quais 13 com cardã de 15° a 20°/s;
  - RCS de 8 propulsores;
  - três grid fins como na V3 (27 m² cada, 50% maiores, mais baixas; duas sobre os pinos de captura em ±y e uma em -z, uma
    disposição suposta): superfícies posicionadas com `normal`, a força normal de cada uma onde está;
  - pinos de captura como `hard_points`, sob as grid fins.
- **Torre:** os braços são `structures`, quatro barras de aço amortecidas em pares, formando trilhos. O pino fica no sulco
  entre as barras. Os braços têm dobradiça no carro da torre: abertos 30° para a subida e para a descida passando por eles,
  fecham a 8°/s quando o veículo paira acima, e a descida até os pinos espera que fechem.
- **Starbase:** o `scenery` desenha a torre em treliça de 146 m, o carro, os braços (que giram com as barras), o braço da
  nave, a mesa de lançamento com as garras, o pátio de concreto, a fazenda de tanques e a linha de propelente (lugares e
  medidas aproximados, de fotos). `--pad` mostra a pilha na mesa com a câmera dando a volta.

**Voo**
1. **Subida:** o programa genérico de subida dirige a pilha pelos cardãs, a 80% de empuxo a partir dos 40 s.
2. **Separação:** com 570 t no booster (15% do propelante), o `spawn` cria o voo `booster`. Os dois voos andam em passo
   travado, cada um com seus comandos por `index`.
3. **Boostback e descida:** o `BoosterReturn` faz o boostback em 13 motores a 60% (uns 55 s, como nos voos), fechando em 3
   quando o pouso previsto chega a um ponto 300 m mar adentro, a leste da torre. Acima do ar o RCS (que gasta o gás dos
   tanques, isto é, propelante) segura a atitude com que ele vai entrar no ar, sem virar no apogeu. No ar, as grid fins
   inclinam o corpo para que a força normal leve o pouso previsto ao ponto de entrada do corredor dos braços (abaixo de
   30 km). O ganho é a força lateral medida pelo IMU por grau de inclinação. A malha de atitude compensa a rigidez
   aerodinâmica do próprio corpo.
4. **Pouso:** a queima de pouso em 13 motores, planejada a 70% (folga para perder motores), resolve a cada 0,2 s o empuxo
   que chega ao portão (80 m/s) na altura certa e inclina o empuxo para corrigir o ponto previsto. Com o ar denso ela
   inclina o corpo para o lado oposto, porque a força normal vence o empuxo lateral; o sinal e o ganho vêm do que o IMU
   mede. Do portão, 3 motores (2 ou 1 quando no mínimo eles erguem demais; mais se faltar) entram pelo corredor até o eixo.
   O rolamento põe os pinos sobre os braços por cardã diferencial. Os braços fecham quando ele está centrado entre os
   trilhos (folga de 0,8 m), e ele desce a 1 m/s até os pinos. Fora do corredor ele mantém a base acima do topo da torre e
   a contorna, nunca passando sobre ela.
5. **Decisões:** a captura é reavaliada o tempo todo: motores com cardã, propelante para terminar dali (incluindo
   contornar), alcance do ponto previsto, se a queda ainda para acima dos braços com todos os motores, tempo sobre os
   braços. Se ela deixa de ser possível, é abandonada de vez. O booster vai para longe da torre: para o mar a leste, ou,
   se estiver a oeste dela, para o interior longe da torre e dos tanques. Ele segura a altura até 150 m da torre e desce
   freando. Sem propelante para fugir, termina a captura. Capturado, ele fica 1 s com os motores no mínimo enquanto os
   braços tomam a carga, descansa 30 s e é tirado dos braços (`EndFlight`).
6. **A nave de volta** ([ship_return.hpp](../examples/ship_return.hpp)), com flaps posicionados (`position_meters`), RCS,
   nós térmicos no escudo e pinos:
   - **Saída de órbita:** em órbita, nariz para a frente e barriga para a Terra, mantidos por um pulso de RCS de 0,5 s a cada
     30 s (sem torque entre eles, em passos longos); procura a cada minuto onde pousaria uma queima agora. Na passagem que
     leva à torre (~22 h depois), vira para trás pela arfagem, queima retrógrado quando apontada a 3° e corta quando o pouso
     previsto chega à torre (dentro dos 2000 km da passagem).
   - **Reentrada:** de barriga, com α de 65° a 80°. Um previsor (tabelas do Albert, U.S. Standard Atmosphere 1976 e
     correção pelo arrasto e pela sustentação medidos) dá a inclinação lateral para o alcance. O lado segue o desvio
     lateral previsto, com reversões dentro de um corredor. Abaixo de 300 m/s, planeio direto para a torre.
   - **Atitude:** os flaps viram o veículo em arfagem e no eixo transversal ao escoamento; o RCS faz o resto (a rolagem
     em volta do escoamento).
   - **Pouso e captura:** giro para os motores, queima em 3 Raptors e pairar em 2 ou 1 (com a linha do empuxo pelo
     centro de massa). Desce a 1 m/s até os pinos, e pendurada nos braços com o motor ainda ligado, o empuxo passa pelos
     pinos até parar de balançar.

**Navegação.** O software de voo não lê a verdade do simulador; ela fica só para as checagens do teste. A pilha, a nave e o
booster voam com IMU de grau navegação, GNSS espacial com RTK da base na torre (até 30 km), rastreador de estrelas e medidores
dos tanques ([navigation.hpp](../examples/navigation.hpp)):
- inercial strapdown no referencial terrestre (RK4, gravidade J2, Coriolis);
- Kalman por eixo com os fixos, recusando os que fogem 5σ da estimativa;
- atitude puxada para a do rastreador, por um modelo próprio da orientação da Terra (precessão IAU 1976 e GMST);
- `q` pela velocidade estimada e a atmosfera padrão, sem o vento.

O booster herda a estimativa da pilha na separação. Na missão o erro fica em ~2–4 m em órbita e 1–2 cm perto da torre.

**Falhas de motor.** Os guiamentos leem dos motores quais falharam (`EngineSample.failed`, o que os sensores de um motor
real dizem) e escolhem os que usam: os primeiros que funcionam numa ordem de preferência (o centro, o anel do meio, os de
fora), trocando na hora um que falhe aceso; com 3 ou menos, o conjunto cujo centro de empuxo fica sobre a linha perpendicular
aos pinos, para que a inclinação que leva o empuxo pelo centro de massa deixe os dois pinos nivelados. A aceleração, a altura do
giro e da queima de pouso contam os motores que funcionam; a captura só desliga os motores depois de 0,3 s nos braços e 1 s
segurando, voltando à descida se escorregar. `--fail=voo@instante-ou-fase:motor,...` injeta falhas (`stack`, `booster` ou
`ship`; o instante em segundos da missão ou a fase do guiamento, como `landing` ou `deorbit`).

O teste `starship_mission` voa tudo e verifica a órbita, as duas capturas, o repouso nos braços, o escudo abaixo do
limite e que nenhum veículo passa pela torre; e voa de novo com seis motores falhando (três dos 33 na subida, dois do booster no
pouso, um da nave na saída de órbita, que pousa nos outros dois). `--warp=N` acelera a órbita em tempo real e `--ship-return` voa só a volta da nave, começando em órbita.

Na Lua o `GENERIC_LUNAR_ASCENT` sobe 50 m e segue pela taxa vertical até 18 km, cortando com
perilúnio ≥ 15 km e apolúnio ≥ 85 km. Em Marte o `GENERIC_MARS_ASCENT` também é guiado desde a
decolagem (no ar rarefeito o gravity turn deixa a subida íngreme demais com empuxo/peso 4,8),
estagia, corta com apoapsis de 250 km, plana até ele e circulariza até o perigeu de 200 km.

O exemplo [landing_client.cpp](../examples/landing_client.cpp) usa o `GENERIC_LANDER` na Lua:
sítio do Apollo 11 com `terrain_height` (LOLA), liberação e ignição, subida a 150 m freando a
~1 m/s², 10 s pairando e descida que desacelera até 1 m/s; o evento `TOUCHDOWN` corta o motor
(pouso a ~0,8 m/s com ~7,7 t de propelante). A tela ainda desenha a Lua lisa, então o sítio, 1,9 km
abaixo da esfera, fica escondido até o relevo regional da fase 4.

```sh
./build/linux/src/rpc/albert_landing_client          # tempo real
./build/linux/src/rpc/albert_landing_client --fast   # o mais rápido possível
```

## Coordenadas e tempo

- Posições em metros e velocidades em m/s, no **ICRF com origem no centro da Terra**.
  Cada amostra deve declarar `GEOCENTRIC_ICRF`; referenciais diferentes são rejeitados.
- Instante em segundos Unix UTC (`utc_unix_seconds`), definido pelo produtor.
  Não é o horário de chegada do pacote. Os estados são avaliados no **instante
  simulado** da cena (`Scene.utc_unix_seconds`), igual ao relógio NTP interno
  enquanto ao vivo. Para acompanhar uma data escolhida, use o tempo simulado;
  dados em tempo real só aparecem ao vivo. Instantes anteriores a 1970 são válidos.
- Quaternion em ordem **X, Y, Z, W**, girando os eixos locais do modelo para o
  ICRF. O Albert normaliza quaternions válidos e rejeita quaternion nulo.
- Os eixos gráficos, a escala e a origem próxima da câmera são cuidados pelo
  render. O cliente não precisa enviar matrizes threepp nem coordenadas em float.

`TimeService.GetTime` e `Scene.clock` expõem o mesmo snapshot temporal do quadro:
qualidade (`LOCAL`, `SYNCHRONIZED`, `HOLDOVER`, `EXPIRED`), UTC, fontes, idade,
última sincronização, incerteza, RTT e geração. `uncertainty_seconds=-1` significa
incerteza desconhecida. `authenticated=false` explicita a ausência de NTS.
Correções grandes mudam `generation`; integrações devem observar esse campo e
suas próprias políticas de cobertura temporal. O Albert preserva o timestamp do
produtor, inclusive quando corrige seu próprio relógio. O exemplo sintético
obtém o UTC inicial dessa API; para apenas consultar:

```sh
./build/linux/src/rpc/albert_demo_client 127.0.0.1:50051 --time
```

A consulta reflete o último quadro, não o instante exato de chegada da resposta.
Detalhes, falhas de rede e política de leap seconds em [TIME.md](TIME.md).

`ClockStatus.simulation` informa o instante simulado (`utc_unix_seconds`), a
velocidade `rate` em segundos simulados por segundo (0 pausado, negativa para trás),
`live`, `limited` (parado em 1900 ou 2100), `generation` (muda ao escolher uma data
ou voltar ao vivo) e `approximate_positions` (Sol, Lua e planetas por modelos
analíticos enquanto as tabelas do JPL não cobrem o instante). `SetTime` escolhe
`utc_unix_seconds` e/ou `rate`; campos ausentes são mantidos e `live=true` volta ao
tempo real. Valores fora de 1900–2100 ou acima de uma semana por segundo retornam
`INVALID_ARGUMENT`. O exemplo `albert_demo_client` carimba seu voo no tempo
simulado.

Amostras devem chegar em ordem estritamente crescente por objeto, inclusive
entre chamadas distintas. Uma chamada rejeitada não aplica parte do seu lote.
Mantenha um produtor coordenando cada objeto para evitar conflitos de sequência.

A posição usa interpolação linear, ou Hermite quando ambas as amostras têm
velocidade. A atitude usa SLERP pelo caminho curto. Para movimento contínuo,
forneça amostras que cubram o instante de exibição, por exemplo com uma pequena
antecipação calculada pelo integrador. O Albert não extrapola indefinidamente:
antes/depois do intervalo disponível mantém o primeiro/último estado. Após um
segundo sem cobertura recente, o objeto é marcado como desatualizado.

`Focus` acompanha o alvo mantendo órbita e zoom manuais. `reset_zoom` restaura
a distância padrão. Em `Orbit`, ângulos são deltas em radianos e
`distance_scale` é um fator multiplicativo opcional; omitido, vale 1.

## Aplicação, limites e encerramento

Chamadas mutáveis retornam `Applied.revision` depois de aplicadas na thread
dona da cena. Isso não significa que a animação da câmera terminou: consulte
`Camera.transitioning`. Consultas usam snapshots consistentes de um quadro;
podem refletir uma revisão anterior por um curto intervalo.

A fila global comporta 256 comandos; cada quadro processa até oito, com orçamento
suave de 2 ms. Cada stream mantém até 32 lotes pendentes e espera o render quando
atinge esse limite. O resumo conta lotes efetivamente aplicados. São permitidos
2.048 objetos na cena, 256 amostras por lote e 256 amostras de histórico por objeto.
Mensagens de entrada têm limite de 1 MiB; recursos do transporte também são limitados.
Dimensões aceitas: 0,1 m a 10 bilhões de metros por eixo.

| Resultado | Código gRPC |
| --- | --- |
| ID desconhecido | `NOT_FOUND` |
| Valores inválidos, ID duplicado, estado fora de ordem ou recurso não suportado | `INVALID_ARGUMENT` |
| Fila ou cena no limite | `RESOURCE_EXHAUSTED` |
| Cena inicializando ou encerrando | `UNAVAILABLE` |
| Comando não aplicado no prazo interno de 3 s | `DEADLINE_EXCEEDED` |

Cancelamento remove a intenção de aplicar comandos ainda pendentes. Uma chamada
cancelada durante a aplicação pode já ter alterado a cena; consulte o snapshot
antes de repetir operações. A desconexão não apaga objetos ou inventa movimento.
No encerramento, comandos pendentes recebem erro e os streams são cancelados.

## Builds cruzados e testes

No Windows nativo, as ferramentas de geração são compiladas para Windows.
Para compilação cruzada pelo Linux, `scripts/build-portable.sh` prepara antes
as ferramentas nativas em `build/portable-host-tools/bin`, isoladas das ferramentas
compiladas diretamente no host. Para fazer apenas o build
cruzado, sem gerar uma distribuição:

```sh
cmake -S scripts/host-tools -B build/host-tools -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/host-tools --target albert_host_tools --parallel 4
cmake -S . -B build/windows-cross -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw64.cmake \
  -DALBERT_HOST_TOOLS_DIR="$PWD/build/host-tools/bin"
cmake --build build/windows-cross --parallel 4
```

As ferramentas nativas precisam funcionar no ambiente que executará o build.
Opcionalmente, `ALBERT_PROTO_GENERATED_DIR` permite reutilizar fontes de protocolo
já gerados pela mesma versão fixada. Esse diretório deve conter `albert/v1/`
e `proto.sha256`, com o SHA-256 de `engine.proto`; diferenças são rejeitadas.

`scene_commands` testa o núcleo; `rpc_commands` testa servidor/cliente reais em
loopback, criação, foco, streaming, remoção, erros e encerramento. A conferência
visual integrada é feita à parte. Referência do transporte:
[documentação oficial gRPC C++](https://grpc.io/docs/languages/cpp/).

A cena padrão inclui 43 corpos. Os 33 adicionais já estão presentes e não precisam
ser importados. Seus elementos offline e limites constam em [LOCAL_SYSTEM.md](LOCAL_SYSTEM.md).
A câmera também aceita `@inner-system` para enquadrar os planetas internos.
