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
| `CatalogService` | `ListObjects`, `ImportObject`, `GetJob`, `GetJobs`, `RetryJob` |

`GetCapabilities` informa os formatos e limites realmente suportados. IDs dos
corpos embarcados: `earth`, `moon`, `sun`, `mercury`. Eles não podem ser removidos nem ter
seus estados substituídos por um cliente. Os alvos especiais `@earth-moon` e
`@system` enquadram as vistas de contexto.

Objetos externos aceitam esfera, caixa e cilindro, com dimensões físicas e cor
RGB. O eixo do cilindro é o Y local; a esfera admite dimensões diferentes por
eixo. Nomes não precisam ser únicos; IDs precisam. Ao remover o objeto focado,
a câmera volta à Terra. Ocultar um objeto preserva seu estado e o acompanhamento.
Modelos, imagens externas e trajetórias de objetos externos desenhadas serão
acrescentados nos próximos marcos.

## Catálogo interno e importação automática

Em **☰ → Dados → Adicionar objetos**, escolha um corpo e clique em **Adicionar e
ver**. A mesma operação está disponível por gRPC; o Albert consulta a fonte,
cria o objeto e renova suas posições sem exigir consultas externas pelo cliente.

```sh
./build/linux/src/rpc/albert_catalog_client 127.0.0.1:50051
./build/linux/src/rpc/albert_catalog_client 127.0.0.1:50051 mars
./build/linux/src/rpc/albert_catalog_client 127.0.0.1:50051 --jobs
./build/linux/src/rpc/albert_catalog_client 127.0.0.1:50051 --remove mars
```

No Windows, o cliente fica em
`build\windows\src\rpc\Release\albert_catalog_client.exe`.
O catálogo inicial contém Sol, Terra, Lua, Mercúrio, Vênus, Marte, Júpiter,
Saturno, Urano, Netuno e Plutão. `ListObjects.query` filtra nomes/IDs. Importações
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

O serviço limita a oito importações na fila, 16 acompanhamentos e 64 operações
no histórico. O adaptador Horizons é compartilhado com o render, serializa as
consultas, mantém até 32 tabelas em memória e valida cobertura, ordem e valores.
Cache de cinco minutos para tabelas curtas e 24 horas para órbitas; respostas
HTTP têm limite de 4 MiB e 30 s. Nenhuma consulta ocorre por quadro. A busca
remota de objetos arbitrários e novos provedores continuam no roadmap.

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
