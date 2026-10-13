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
| `TimeService` | `GetTime` |
| `LayerService` | `CreateLayer`, `ListLayers`, `SetVisibility`, `RemoveLayer` |
| `AssetService` | `RegisterModel`, `ListModels`, `RemoveModel` |
| `CatalogService` | `ListObjects`, `ImportObject`, `GetJob`, `GetJobs`, `RetryJob` |

`GetCapabilities` informa os formatos e limites realmente suportados. IDs dos
corpos embarcados: `earth`, `moon`, `sun`, `mercury`, `venus`, `mars`, `jupiter`, `saturn`, `uranus`, `neptune`. Eles não podem ser removidos nem ter
seus estados substituídos por um cliente. Os alvos especiais `@earth-moon` e
`@system` enquadram as vistas de contexto.

Objetos externos aceitam esfera, caixa, cilindro e modelo GLB registrado, com dimensões físicas e cor
RGB. O eixo do cilindro é o Y local; a esfera admite dimensões diferentes por
eixo. Nomes não precisam ser únicos; IDs precisam. Ao remover o objeto focado,
a câmera volta à Terra. Ocultar um objeto preserva seu estado e o acompanhamento.
Imagens externas e trajetórias de objetos externos desenhadas serão acrescentadas
nos próximos cortes. O primeiro suporte a modelos está descrito abaixo.

## Catálogo interno e importação automática

No seletor da barra superior, pesquise um corpo e clique no resultado com **+**. A mesma operação está disponível por gRPC; o Albert consulta a fonte,
cria o objeto e renova suas posições sem exigir consultas externas pelo cliente.

```sh
./build/linux/src/rpc/albert_catalog_client 127.0.0.1:50051
./build/linux/src/rpc/albert_catalog_client 127.0.0.1:50051 europa
./build/linux/src/rpc/albert_catalog_client 127.0.0.1:50051 --jobs
./build/linux/src/rpc/albert_catalog_client 127.0.0.1:50051 --remove europa
```

No Windows, o cliente fica em
`build\windows\src\rpc\Release\albert_catalog_client.exe`.
O catálogo local contém 71 corpos: Sol, oito planetas, Lua, Plutão e 60 luas adicionais. `ListObjects.query` filtra nomes/IDs. Importações
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
HTTP têm limite de 4 MiB e 30 s. Nenhuma consulta ocorre por quadro. `SearchObjects({query:"Apophis"})` consulta a NASA/JPL SBDB (API 1.3).
A pesquisa é explícita, por nome/designação; aceita até 40 caracteres ASCII,
incluindo `*` para padrões, e retorna até 50 resultados. `ListObjects` continua
local: são 71 corpos, incluindo 60 luas. A UI usa os mesmos serviços.

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

## Coordenadas e tempo

- Posições em metros e velocidades em m/s, no **ICRF com origem no centro da Terra**.
  Cada amostra deve declarar `GEOCENTRIC_ICRF`; referenciais diferentes são rejeitados.
- Instante em segundos Unix UTC (`utc_unix_seconds`), definido pelo produtor.
  Não é o horário de chegada do pacote. O Albert usa sua referência NTP interna.
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
