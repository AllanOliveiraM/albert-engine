# Desenvolvimento do Albert Engine

Simulador astronômico em C++20, com threepp/OpenGL, Dear ImGui, JPL Horizons e ERFA.

O ponto de entrada fica em `src/main.cpp`. O módulo `src/render/` contém a janela,
cena, interface e carregamento dos dados, expostos pela interface `application.hpp`.

## Compilar no Linux

Ubuntu 24.04 ou mais recente, Debian 12 ou mais recente e Fedora são preparados
pelo script abaixo. Ele instala os pacotes de desenvolvimento com o gerenciador
do sistema e pode solicitar sua senha de administrador:

```sh
bash scripts/setup-linux.sh
make build
make run
```

Se os pré-requisitos já estiverem instalados, basta executar `make render-build`.
No Ubuntu/Linux, use `make desktop` uma vez após compilar para associar o ícone
ao aplicativo e criar o atalho no menu do seu usuário. Isso registra somente
metadados e o ícone em `~/.local/share`, sem instalar ou mover o executável.
Reabra o programa; se já estava fixado na barra, remova o favorito antigo e
fixe novamente. Repita o comando se mover a pasta do projeto ou do executável
(`python3 scripts/register-desktop.py --executable /caminho/AlbertEngine`).
`make run` compila apenas o alvo `render`; `make build` e `make test` também
preparam os testes e o cliente de exemplo. Os wrappers Linux/Windows serializam
configuração e compilação por pasta para evitar dois builds simultâneos.
Chamadas diretas ao CMake precisam ser coordenadas pelo desenvolvedor.
O scanner de módulos C++ fica desligado: os fontes usam headers, sem módulos.
Os builds usam dois trabalhos simultâneos por padrão. Em máquinas com pouca
RAM, use `make build JOBS=1` ou `.\make.cmd build -Jobs 1`; evite compilar
Linux e Windows ao mesmo tempo.

A configuração versionada de VS Code aponta para os fontes atuais e exclui
`build/`, `dist/`, `.tools/` e `asset-packs/` da indexação e dos observadores.
O IntelliSense tem orçamento de 1024 MB por processo. Para limitar também o
serviço C++ completo, coloque nas **configurações do usuário** (essas opções
não aceitam escopo de workspace):

```json
"C_Cpp.maxMemory": 2048,
"C_Cpp.maxConcurrentThreads": 2,
"C_Cpp.intelliSense.maxMemory": 1024,
"C_Cpp.references.maxMemory": 512,
"C_Cpp.codeAnalysis.maxMemory": 512
```

São orçamentos do serviço, não limites rígidos de RSS impostos pelo sistema.
Se o serviço já estiver usando muita memória, reinicie o VS Code para descartar
o estado antigo. O CMake gera `build/linux/compile_commands.json` no Linux.
O wrapper Linux também executa `ninja -t recompact` antes do build, evitando
recompilações repetidas após corrupção do índice em versões afetadas do Ninja
([bug upstream](https://github.com/ninja-build/ninja/issues/2703)). Se entradas
de dependência já foram perdidas, sua reconstrução ainda é necessária uma vez.
São necessários CMake >= 3.25, compiladores C/C++20 (GCC 12+ recomendado), Ninja,
pkg-config, certificados CA, headers OpenSSL e X11/OpenGL. Os nomes exatos dos
pacotes estão em `scripts/setup-linux.sh`. Em sessões Wayland, é necessário XWayland,
pois a GLFW incluída usa X11 no Linux. Para outra distribuição, instale os
equivalentes desses pacotes e use os mesmos comandos CMake.

Sem Make:

```sh
cmake --preset linux
cmake --build --preset linux
ctest --preset linux
./build/linux/bin/AlbertEngine
```

## Compilar no Windows

Windows 10/11 x64. O script baixa as ferramentas diretamente dos fornecedores,
sem Microsoft Store ou winget. O CMake fica em `.tools`; se necessário, instala
Microsoft Visual Studio Build Tools 2022 com compilador C++ e Windows SDK.
Essa instalação é necessária apenas na máquina que compila e pode solicitar UAC
ou reinicialização. Usuários do executável distribuído não precisam dessas ferramentas.

```powershell
.\make.cmd setup
.\make.cmd build
.\make.cmd run
```

Com CMake >= 3.25 e Visual Studio 2022/Build Tools com a carga **Desktop development
with C++** já instalados:

```powershell
cmake --preset windows
cmake --build --preset windows
ctest --preset windows
.\build\windows\bin\Release\AlbertEngine.exe
```

Não precisa de MSYS2, Make, vcpkg nem bibliotecas instaladas manualmente no Windows.

## Pasta de distribuição

Os mesmos alvos servem nos dois ambientes. No Windows, `make.cmd` chama o
PowerShell incluído no sistema; não exige GNU Make. No Linux, use `make`.

| Ação | Linux | Windows (PowerShell) |
| --- | --- | --- |
| Preparar ferramentas/dependências do sistema | `make setup` | `.\make.cmd setup` |
| Compilar | `make build` | `.\make.cmd build` |
| Compilar e abrir | `make run` | `.\make.cmd run` |
| Compilar e testar | `make test` | `.\make.cmd test` |
| Testar e gerar distribuição limpa | `make dist` | `.\make.cmd dist` |
| Preparar tudo e gerar distribuição | `make bootstrap` | `.\make.cmd bootstrap` |
| Limpar o build nativo | `make clean` | `.\make.cmd clean` |

Os nomes anteriores `render-build`, `render-run`, `render-test`, `render-dist`
e `render-clean` continuam disponíveis. Para limitar compilação paralela:
`make dist JOBS=2` ou `.\make.cmd dist -Jobs 2`.

A distribuição fica pronta para zipar, com **o próprio executável na raiz**:

```text
dist/linux/                  dist/windows/
  AlbertEngine                 AlbertEngine.exe
  assets/                      assets/
  lib/                         licenses/
  licenses/
```

Abra `AlbertEngine` ou `AlbertEngine.exe`, inclusive com duplo clique conforme
as permissões/políticas do gerenciador de arquivos. Não há lançador intermediário
nem pasta `bin` na distribuição. Mova ou zipe a pasta inteira. Cada sistema e
arquitetura requer seu próprio build. Os comandos de distribuição recriam sua
pasta de destino, sem incluir fontes, testes, logs ou cache de execução.
`licenses` reúne avisos obrigatórios. README, documentação e metadados de build
ficam somente no repositório; o pacote inclui apenas os arquivos de execução
e os avisos necessários para redistribuição.

O comando CMake usado internamente, `cmake --install ... --prefix dist/...`,
apenas copia arquivos para essa pasta; não instala o aplicativo no sistema,
nem cria atalhos. A preparação de ferramentas instala somente dependências
necessárias na máquina de desenvolvimento.

No Windows, as bibliotecas do projeto e o runtime C/C++ são vinculados estaticamente:
o programa usa TLS/CA nativos do Windows e não exige Visual C++ Redistributable.
No Linux, a pasta inclui as bibliotecas redistribuíveis detectadas, incluindo
OpenSSL e o runtime C++. A glibc, certificados CA, servidor gráfico e driver OpenGL
>= 3.3 são fornecidos pela máquina de destino. Um build Linux exige glibc da mesma
versão ou mais recente que a usada para compilar; para distribuir amplamente,
compile na distribuição mais antiga que pretende suportar. O CI usa Ubuntu 24.04
como base; um build feito em Ubuntu 26.04 não promete funcionar em versões anteriores.
Não há garantia de um único binário funcionar em todas as distribuições Linux.

Para produzir as duas pastas a partir do Linux, usando Docker já instalado e uma
base Linux mais antiga, sem instalar compiladores no host:

```sh
bash scripts/build-portable.sh all
```

Use `linux` ou `windows` no lugar de `all` para gerar apenas uma plataforma.
O ambiente é declarado em `scripts/Dockerfile.build`: Debian 12, GCC 12/CMake 3.25
e MinGW-w64 para Windows x64. O Linux resultante usa a base glibc 2.36; o Windows
inclui estaticamente os runtimes MinGW e usa apenas DLLs do próprio sistema.
Os testes Linux são executados no container. Testes do executável Windows precisam
de Windows ou Wine e não são executados automaticamente por esse script.
O caminho `build/portable-*` separa esses builds dos builds nativos.

## Dependências declaradas

As versões e os SHA-256 ficam em `cmake/Dependencies.cmake`. O CMake baixa e compila
threepp (commit fixo, incluindo GLFW/GLAD e suas dependências), Dear ImGui 1.92.6,
nlohmann/json 3.12.0, ERFA 2.0.1 e libcurl 8.22.0. Recursos extras da threepp e
dependências opcionais da libcurl são desativados. A libcurl usa OpenSSL no Linux
e Schannel no Windows. As licenças acompanham a distribuição em `licenses/`.

A primeira configuração precisa de internet. Depois dos downloads, os fontes
ficam em `build/<plataforma>/_deps`; não apague essa pasta para recompilar offline.
É possível configurar com `-DFETCHCONTENT_FULLY_DISCONNECTED=ON` **após** os fontes
terem sido baixados. Não há download de bibliotecas na inicialização do programa.

## Assets, dados e execução

A distribuição guarda toda a biblioteca de imagens em `assets/visuals.pak`.
Catálogo estelar, fonte, paleta meteorológica e fronteiras ficam organizados no
mesmo diretório. `assets/manifest.json` verifica os dados auxiliares no build;
o índice do pacote declara e verifica SHA-256 de cada bloco lido.
Não há downloads de superfícies durante a execução. A biblioteca está versionada em `assets/imagery/` com Git LFS. O build reúne
as partes automaticamente e verifica SHA-256, sem processar as imagens novamente.
A regeneração a partir dos originais usa Docker, separadamente da compilação.
`ALBERT_ASSET_DIR` e `ALBERT_PACK_PATH` permitem caminhos explícitos no desenvolvimento.
O executável distribuído resolve assets a partir de sua própria localização.
O build de desenvolvimento também procura o pacote em `build/<ambiente>/imagery/visuals.pak`;
`ALBERT_ASSET_PACK` é o caminho configurável no CMake.

Dados dinâmicos são consultados automaticamente em segundo plano:

| Dados | Fonte | Frequência de consulta |
| --- | --- | --- |
| Posições do Sol/Lua | NASA/JPL Horizons | 5 minutos; interpolação Hermite entre amostras de 5 minutos |
| Trajetórias orbitais | NASA/JPL Horizons | 24 horas |
| Fração de nuvens observada | NASA GIBS, Terra+Aqua/MODIS | 30 minutos; mosaico diário atualizado por passagens |

As nuvens MODIS **não são uma fotografia global instantânea**: passagens diurnas/noturnas de Terra e Aqua são combinadas e têm atraso de
processamento. Lacunas do dia atual usam observações do dia anterior quando
disponíveis. A interface informa o intervalo de datas e a cobertura válida. Pixels sem observação não recebem nuvens
inventadas. Antes da primeira imagem válida, a camada permanece transparente.
Em caso de falha, a última imagem
válida é preservada, sua data continua visível, e novas tentativas são
espaçadas automaticamente. A máscara visual combina a fração de nuvens com detalhes das imagens
True Color diurnas Terra/Aqua. Onde falta reflectância válida, não se inventa estrutura de nuvens:
a camada permanece transparente. A fração de cobertura de células de 5 km é
suavizada somente como confiança visual da presença de nuvens. A reflectância
fornece sua estrutura e opacidade, sem multiplicar duas vezes a mesma cobertura;
isso reduz blocos de opacidade ao aproximar. A cobertura informada
continua calculada sobre os pixels observados originais. As bordas das passagens são suavizadas
com observações disponíveis dos outros instrumentos/dias. Neve e solo claro podem gerar ambiguidades; não há
reconstrução volumétrica. A cobertura informada mede pixels válidos da fração,
não a cobertura fotográfica. Terra e Aqua são consultados separadamente
para evitar que lacunas pretas opacas de um instrumento escondam dados do outro.
O último cache válido aparece imediatamente. Uma consulta concluída há menos de
30 minutos é reutilizada até a próxima atualização, inclusive ao reabrir o programa. O mosaico global usa seis imagens e uma data; recortes regionais são mantidos no mesmo cache com limite configurável.

A simulação precisa de internet para iniciar com efemérides reais; não substitui
posições por dados inventados. A janela abre antes das consultas, informa a
etapa e os bytes recebidos, e oferece **Tentar novamente**. Falhas de rede/TLS
não congelam a interface. No menu lateral, em **Dados**, **Atualizar posições agora** renova os dados.
Em **Manutenção dos arquivos**, **Recarregar texturas incluídas** relê os
arquivos locais e **Limpar e baixar nuvens** remove somente o cache meteorológico do Albert. Assets do
repositório/distribuição não são apagados. Fechar a janela cancela as transferências.
Falhas de inicialização gráfica aparecem no terminal (Linux) ou em uma janela
de erro (Windows, ao abrir sem console). O executável Windows abre diretamente
a interface, sem uma janela de console adicional.

Após o primeiro quadro completo da cena, o cartão de carregamento desaparece
suavemente e revela a simulação e seus controles em aproximadamente um segundo.
A compilação inicial dos shaders não consome essa animação. Nenhum efeito de
pós-processamento ou renderização adicional da cena é necessário.

O cache meteorológico é criado em `data/clouds`, ao lado do executável,
somente durante a execução. `ALBERT_DATA_DIR` permite escolher outra pasta.
Assets incluídos não são modificados. Se o executável não tiver a pasta `assets`
ao lado (uso avançado com `ALBERT_ASSET_DIR`), o cache usa `$XDG_CACHE_HOME` ou
`$HOME/.cache/albert-engine` no Linux e `%LOCALAPPDATA%\AlbertEngine\Cache` no
Windows. O programa não altera registro, associações, atalhos ou serviços e
não grava arquivos de log.

## Visualização e precisão

Terra elipsoidal WGS84, base global até 4K e blocos regionais de um mosaico 86400×43200, filtragem
anisotrópica/mipmaps, antialiasing MSAA 4x, iluminação solar, reflexos oceânicos,
luzes noturnas, camada de nuvens e atmosfera por espalhamento simples Rayleigh/Mie
(8 amostras no raio da câmera; profundidade óptica solar consultada em uma
tabela 256×128 calculada uma única vez com 128 amostras por raio; alturas de
escala de 8/1,2 km). A tabela evita integrar um segundo raio a cada pixel.
O material terrestre usa reflexão dielétrica, rugosidade distinta para oceanos
e continentes e relevo GEBCO na escala física. Luzes NASA Black Marble emitem
somente no lado noturno. O Sol usa intensidade HMI de referência, com
escurecimento do limbo e cor visual próxima do branco. A interface usa a fonte Inter, uma barra de foco integrada ao menu hambúrguer
e um painel lateral animado com abas Cena, Dados e Sobre. Informações técnicas
ficam em seções expansíveis. As trocas de foco usam easing de quinta ordem
e duração de 1,4 a 2,2 segundos, limitada mesmo entre grandes distâncias; zoom
e rotação têm amortecimento ajustado ao tempo entre quadros.
O menu abre e fecha em 240 ms, com a mesma curva nos dois sentidos.
O botão flutuante **Restaurar zoom** aparece quando a distância sai do padrão;
a restauração preserva o ângulo da câmera e usa easing de 0,9 a 1,3 segundos.
Os controles de qualidade limitam a resolução em telas HiDPI e o tamanho das
sombras. Sombras PCF são atualizadas no máximo a 10 Hz; decodificação e
redimensionamento de imagens ficam fora da thread gráfica. Não há pós-processamento
em tela inteira ou ray tracing obrigatório.

O catálogo HYG 4.1 inclui 5.070 estrelas de magnitude visual ≤ 6. Direções J2000
são corrigidas pelas velocidades do catálogo até a data atual; cores aproximam
B−V e brilho segue magnitude com compressão para a tela. O céu acompanha a
posição da câmera para representar estrelas no infinito; não é uma simulação
de paralaxe, aberração, variabilidade estelar ou brilho fotométrico calibrado.
O tamanho dos pontos é um auxílio visual. A seleção está sob CC BY-SA 4.0,
conforme `assets/README.md`.

O núcleo e os provedores mantêm posições SI/double; o adaptador legado de
órbitas ainda usa km/double até a conversão gráfica. A rotação terrestre
usa ERFA; DUT1 e movimento polar ainda são zero. A Lua usa a aproximação
trigonométrica IAU_MOON do NASA/NAIF `pck00011`, validada contra CSPICE; não usa
PCK lunar binário de alta precisão. Mapas de relevo, vegetação, cidades e manchas
solares são estáticos. Atmosfera (sem espalhamento múltiplo), reflexos inferidos do mapa e luz auxiliar
lunar são recursos visuais, separados das posições/dimensões astronômicas.
A opção de clarear o lado escuro da Lua usa a direção solar transformada para
coordenadas lunares e atua somente no hemisfério sem iluminação direta, com
transição no terminador. A face oculta e a face voltada à Terra não determinam
essa máscara. O mesmo preenchimento acompanha os recortes de alta resolução.
O auxílio começa desligado na Terra e na Lua. Ao ativar, o brilho inicial é
25% na Lua e 22% na Terra, com controles independentes. Ligar, desligar ou ajustar
a intensidade usa uma transição reversível que percorre aproximadamente 95%
da diferença em 240 ms, independente do FPS. Esses valores são controles visuais,
sem interpretação como intensidade astronômica medida. Luzes das cidades
permanecem uma camada separada. Atmosfera, nuvens, cidades, fronteiras e órbitas
também entram e saem suavemente; efeitos desligados deixam de ser desenhados
ao fim da transição. Os botões não exibem tooltips redundantes.

## Validação

```sh
make render-test
./build/linux/bin/AlbertEngine --smoke-test
```

Os testes CTest verificam conversão UTC, caminho de cache, estado/cancelamento dos
downloads, orientação lunar contra NASA/NAIF, direção de Sirius/seleção estelar e
as duas suítes oficiais da ERFA. `visual_controls` verifica defaults da luz,
transições em diferentes FPS, latitude WGS84 e preservação de pequenas ilhas
na simplificação das fronteiras. Para verificar HTTP 503, nova tentativa
e cancelamento durante uma transferência, com Python 3 opcional:

```sh
python3 tests/http_fixture.py build/linux/tests/download_test
```

No Windows, use `python tests/http_fixture.py build/windows/tests/Release/download_test.exe`.
Python 3 também reúne as partes dos assets no build Linux e executa o teste do
pacote visual. No build Windows, a montagem usa PowerShell; Python habilita os
testes opcionais e já está presente nos runners de CI.
O `--smoke-test` consulta dados reais, cria uma janela OpenGL, renderiza
cinco quadros e encerra; precisa de internet e de uma sessão gráfica (ou Xvfb no
Linux). Falha se as efemérides não puderem ser usadas. `--help` não precisa de rede.
O workflow `.github/workflows/build.yml` compila e testa Linux e Windows.
A biblioteca grande entra por Git LFS, inclusive no checkout do CI. CI não substitui teste visual em uma
máquina Windows real.

Com uma sessão OpenGL disponível, `build/linux/tests/visual_test --graphics`
verifica órbitas atrás/à frente dos recortes da Terra e Lua, cobertura de tracejado
subpixel, fronteiras sobre relevo de 6 km e oclusão no hemisfério oposto.
No Windows, o executável correspondente fica em `build/windows/tests/Release/visual_test.exe`.
Esse teste gráfico é opcional e separado do CTest para não exigir uma janela no CI.


As órbitas são visíveis por padrão. Em **Cena**, as vistas **Terra e Lua** e
**Sistema solar** enquadram as trajetórias em escala real, vistas acima do plano
orbital. Uma vista aproximada da superfície não contém a órbita inteira.


## Detalhes conforme o zoom e cache

A biblioteca completa acompanha as duas distribuições em `assets/visuals.pak`,
com os mesmos bytes e hashes. Ela não é extraída nem copiada para o cache.
O índice é carregado uma vez; cada worker abre seu próprio stream e lê somente
os blocos necessários. JPEG/PNG já são comprimidos, portanto os payloads são
armazenados diretamente, com offsets de 64 bits e SHA-256. O formato próprio
impede a abertura casual como pasta de imagens, mas não é criptografia/DRM.

A visão global usa níveis menores da mesma pirâmide. Ao aproximar, um worker
monta a região visível, com margem e nível de resolução adequado, até 4096 pixels
por eixo para cor/cidades e 1024 para elevação. A região é limitada pelo campo
de visão completo, incluindo proporção da janela e polos; longitudes se repetem
na emenda. Os dois pixels de sobreposição permitem amostragem bilinear entre blocos.
A memória fica limitada às imagens globais, aos recortes ativos e a um bloco
decodificado por vez; não é proporcional ao tamanho do arquivo em disco.
Recortes começam a ser preparados durante a navegação, sem exigir pausa da
câmera. Pequenas mudanças preservam trabalhos que ainda cobrem a vista; mudanças
para outra região/corpo cancelam trabalhos sem utilidade. Há um recorte residente
por corpo e uma preparação/upload em andamento, com fila limitada.
A base continua disponível durante o carregamento; falhas oferecem nova tentativa.

Terra/cidades têm fonte de aproximadamente 500 m/pixel no equador; Lua, 101 m/pixel.
O relevo GEBCO08 terrestre e LOLA lunar desloca as malhas na escala física e
alimenta as normais dos materiais. A Lua conserva os 16 bits do DEM ao decodificar:
elevação em metros = -10000 + amostra × 0,5, relativa ao raio de 1737400 m.
Recortes usam malha 192×192 com afastamento visual de 20 m. A superfície
global é mascarada no interior do recorte, impedindo que uma interpolação
grosseira encubra depressões do DEM; o recorte escreve sua própria profundidade
no passe opaco, antes das linhas transparentes de órbita. Terra global, recortes,
nuvens e cidades usam latitude geodética nos mapas, coerente com WGS84.
O relevo é limitado pela resolução do DEM, sem edifícios nem síntese de crateras.
A fonte GEBCO original é JPEG de 8 bits, portanto o PNG evita perdas adicionais.
A imagem solar passa de 2K para 4K conforme a distância; a fotosfera não recebe
relevo sólido. O hemisfério observado se mistura suavemente ao modelo de Sol quieto.
Aproximar além da resolução das fontes não cria informação adicional.

Nuvens próximas usam recortes WMS 2048² que cobrem a interseção do campo
de visão com o globo, incluindo a proporção da janela e uma margem sobreposta.
Os limites são quantizados em uma grade estável. Consultas regionais têm um
intervalo mínimo de 250 ms entre inícios, sem exigir 900 ms de câmera parada.
São usadas as mesmas datas/instrumentos do mosaico global. Um recorte substitui
suavemente a máscara global, em vez de somar opacidades, e também atenua as luzes
noturnas. O detalhe é limitado pela reflectância MODIS (~250 m) e pela fração
científica mais grosseira; nunca é uma fotografia instantânea ou nuvem volumétrica.
Recortes sem observações utilizáveis mantêm a imagem global. Há uma consulta
regional por vez, com retentativas espaçadas, e os resultados são reutilizados em disco.

`Dados → Cache e detalhes visuais` mostra o espaço ocupado, permite limpar e
configura 128 MB–8 GB (padrão 2 GB), persistido em `data/cache-settings.json`.
Somente dados temporários em `data/assets` (legado) e `data/clouds` são gerenciados. A remoção usa a data do
último uso: os arquivos menos usados saem primeiro. Arquivos temporários de
transferências em andamento não são removidos; o limite converge após os downloads.
A limpeza não apaga assets incluídos nem a imagem já residente na GPU.

As órbitas usam datas TDB relativas a uma época próxima, evitando perda de
precisão em floats na GPU. Horizons fornece ±14 dias para a Lua e ±185 dias
para a Terra. Subdivisão Hermite evita curvas angulosas. O passado é azul contínuo;
o futuro, âmbar tracejado. Ambos desaparecem gradualmente nas extremidades,
com aproximadamente uma volta útil visível. As vistas de contexto mantêm tamanhos
reais e marcam o presente junto aos corpos. As vistas próximas também mostram os trajetos selecionados, com oclusão pelos corpos.
As linhas são faixas trianguladas com suavização e largura em pixels, evitando
a limitação de linhas de 1 px de alguns drivers Windows. O tamanho dos traços
acompanha a distância da câmera para permanecer legível durante o zoom.
Os vértices físicos permanecem em double e são convertidos para coordenadas
relativas à câmera antes do envio à GPU, evitando tremores pela subtração de
coordenadas astronômicas em float. O antialiasing dos traços usa a fase contínua,
sem picos de derivada na repetição do tracejado. A cobertura é integrada sobre
a área de um pixel: traços pequenos demais se misturam gradualmente numa linha
contínua, conservando a cor âmbar do futuro.

As fronteiras usam faixas suavizadas com largura em pixels e amostram o mesmo
DEM global/regional da Terra. Um afastamento visual dependente da distância
cobre a interpolação dos segmentos; o teste de profundidade permanece ativo.
Há dois níveis de geometria, com histerese na troca: contornos simplificados
na vista global e subdivisão máxima de 0,05° perto da superfície. Regiões de 5°
compartilham buffers, mas têm limites próprios para descartar partes fora da
câmera, evitando desenhar o mapa inteiro no zoom. A resolução cartográfica
continua limitada ao Natural Earth embarcado; isso não acrescenta ruas ou
fronteiras mais precisas à fonte original.

A rotação e o passo de zoom diminuem com a altitude relativa ao corpo, permitindo
ajustes finos nas vistas próximas sem alterar a escala física.

Os recortes de superfície, cidades e nuvens incluem toda a área visível, em vez
de selecionar apenas a célula abaixo da câmera. O cálculo usa o raio físico,
altitude, campo de visão e proporção da janela. Regiões cruzando ±180° são
separadas em duas consultas WMS válidas e combinadas sem perder os pixels
na emenda. Consultas que perderam cobertura da vista são canceladas; recortes ainda úteis
podem ser publicados durante o movimento e refinados em seguida.

## Preparação da biblioteca visual

Veja [Preparar assets de alta resolução](ASSETS.md). As partes versionadas via Git LFS são reunidas no build. O pacote é regenerado
com ferramentas próprias em Docker e incluído integralmente em cada distribuição.

## Núcleo de cena e controle externo

`src/core` mantém IDs, estado SI/ICRF em double, histórico limitado, interpolação
com timestamps do produtor e comandos/snapshots independentes da threepp.
`src/render/objects` adapta descritores de esfera, caixa e cilindro. Terra/Lua/Sol
mantêm seus materiais e provedores próprios, registrados pelos mesmos IDs.
A origem gráfica acompanha o alvo; a subtração física ocorre antes da conversão
para float. Câmera, órbitas e fronteiras acompanham essa mudança de origem.

O gRPC é compilado por padrão e ativado somente com `--rpc`/`--rpc-port=N`.
Versões e checksums estão em `cmake/Grpc.cmake`. Protoc e o plugin C++ são
compilados e usados pelo CMake; builds cruzados precisam das ferramentas nativas.
Contrato, limites, exemplo e instruções estão em [RPC.md](RPC.md).

## Publicação de detalhes sem bloquear a navegação

Geometria, normais e máscara do terreno regional são preparados no worker que lê
as imagens. A thread gráfica recebe texturas ainda não publicadas e transfere
blocos de até 1 MiB, com teto de 8 MiB e orçamento suave de 2 ms por quadro.
Alocação e geração de mipmaps recebem quadros separados. A malha/textura antiga
continua sendo exibida até o novo conjunto estar pronto; câmera e UI permanecem
operantes. O detalhe solar também usa essa fila. O orçamento não constitui um
limite absoluto para o driver OpenGL; desempenho deve ser medido em GPU física.

Nuvens começam desligadas, sem consultas automáticas. Quando ativadas, recortes
novos se misturam ao anterior com transição temporal, além da margem espacial.
O loader de detalhes tem atraso de 150 ms, fade e posição fixa acima do botão de
zoom; não muda o tamanho do painel de estado nem desloca outros controles.

## Provedores e conteúdo interno

`src/net/` compartilha HTTP/cancelamento; `src/providers/` valida e armazena
vetores Horizons; `src/catalog/` gerencia importações e renovação; `src/core/`
aplica comandos e interpola estados. UI e gRPC usam o mesmo catálogo. Apenas a
thread gráfica modifica a cena; o worker aguarda a aplicação e expõe operações
assíncronas com revisão, fonte, validade e falhas. O render continua atualizando
objetos independentes quando efemérides de corpos embarcados ficam indisponíveis.

`core/astro_time` usa ERFA para UTC/TAI/TT/TDB e a correção geocêntrica completa
`eraDtdb`; não usa uma aproximação de dois termos para consultar o Horizons.
`astronomical_providers` testa conversões, CSV, unidades, cobertura, cache,
serialização e cancelamento. `catalog_import` testa importação/foco, atualização,
falhas, recuperação, snapshots atrasados, propriedade do estado, remoção e RPC.
`http_transport` exercita servidor HTTP local, truncamento, limite de
resposta e cancelamento. As consultas públicas reais são verificações separadas:
`providers_test --network`.

## Modelos externos — primeiro corte

`core/model` decodifica um subconjunto GLB estático para geometria validada e
materiais sólidos. Não chama loaders com acesso a arquivos externos. O trabalho
ocorre no RPC antes de enfileirar `InstallModel`; a cena conserva assets imutáveis
e metadados pequenos nos snapshots. O render compartilha buffers por asset,
instancia peças, conserva a origem do modelo e aplica a mesma transformação
ICRF e origem flutuante dos outros objetos. Estados não retransmitem geometria.

`external_models` verifica limites, hierarquia/reflexão, PBR, bytes inválidos,
URIs, ciclos, escala física e ciclo de vida. `rpc_commands` inclui registro,
consulta, criação/foco/estado e remoção de modelo/asset por transporte real.
O teste gráfico também exercita GLB a 100 milhões de km, remoção e recriação de
ID. O orçamento deste primeiro corte é para modelos pequenos sem texturas;
a inicialização de buffers gráficos continua sujeita ao custo do driver.
Os limites e o exemplo estão em [RPC.md](RPC.md). A validação local com
AddressSanitizer/UndefinedBehaviorSanitizer também exercitou 5.000 mutações
determinísticas de GLB, sem erros de memória/comportamento indefinido.

## Camadas vetoriais — primeiro corte

`core/layers` prepara coordenadas e subdivisão cartográfica; `Scene` controla
IDs, limites e propriedade das camadas. A API aplica os mesmos comandos usados
pela UI. `render/layers` desenha faixas e marcadores com tamanho em pixels,
antialiasing, profundidade e transições. Camadas ancoradas copiam posição/rotação
do alvo e conservam buffers; coordenadas ICRF são convertidas de double apenas
após subtrair a origem gráfica. A superfície terrestre reutiliza as texturas de
relevo e a transformação WGS84 das fronteiras. Materiais espaciais usam um shader
sem amostradores de terreno; os materiais terrestres renovam os bindings também
durante a transição de remoção, evitando referências a texturas já substituídas.

`vector_layers` verifica WGS84, linha de data, precisão SI, validação atômica,
visibilidade, limites e remoção de referências. `rpc_commands` testa o serviço
real e distingue a visibilidade de objetos da visibilidade de camadas. O teste
OpenGL verifica marcadores atrás/à frente de uma caixa, transição, precisão a
1e11 m e linhas sobre relevo de 6 km, incluindo oclusão do lado oposto da Terra.


## Expansão planetária e pesquisa de objetos

Os oito planetas usam o registro comum; os seis novos mapas e limites estão em
`assets/solar-sources.json`. `scripts/prepare-solar-assets.py` requer Pillow/NumPy,
verifica fontes e empacota UVs reprojetadas dos modelos NASA. A biblioteca continua
única no build. `scripts/import-planet-frames.py` regenera orientação/raios pck00011;
fixtures de CSPICE são independentes. Não há DEM artificial derivado de imagens.

`surfaceBodies()/surfaceBody()` descrevem superfícies e orientações separadamente
de `naturalBodies()`, que permanece sendo o conjunto de planetas dos provedores
online. O primeiro lote de luas usa `moon_frames.inc` (IAU pck00011), mapas NASA
VTAD fixados em `assets/moon-sources.json` e `scripts/prepare-moon-assets.py`.
O pacote `imagery/moons/` é unido automaticamente à biblioteca; não cria consultas
Horizons extras nem objetos duplicados. LOD, uploads incrementais, fades, iluminação
auxiliar e descarte de recortes são os mesmos de `SolidBodies`. Seleção usa a esfera
local da malha, evitando percorrer triângulos a cada hover. Detalhes e limitações
em [LOCAL_SYSTEM.md](LOCAL_SYSTEM.md#mapas-das-luas-de-júpiter).
O fixture opcional `python3 tests/moon_assets_fixture.py` usa Pillow, sem rede;
`natural_bodies` testa orientação contra CSPICE e `visual_test --graphics` exercita
renderização, clique, escala, upload e descarte para as quatro luas.

`providers/small_bodies` consulta SBDB API 1.3, valida respostas e resolve designações
em SPK/diâmetro. `catalog::Manager::search` atende à UI assíncrona e a `SearchObjects`.
IDs SBDB são opacos; o worker confirma o SPK antes de buscar vetores Horizons com
`DES=<SPK>;`. A cena declara quando o diâmetro é desconhecido e o tamanho é visual.
Limites: 50 resultados/pesquisa, cache de 32 por cinco minutos, HTTP 2 MiB/20 s,
16 importações na fila, 128 acompanhamentos e 256 registros. Acesso JPL serializado.

O seletor superior reúne a cena e os catálogos sem controles duplicados. Curva de
voo de quinta ordem em `animation.hpp`, duração limitada por escala/distância e
exposição fotográfica gradual. Materiais auxiliares PBR preservam a iluminação
solar, têm brilho individual por instância e permanecem desativados por padrão.
Buffers de geometria de modelos continuam compartilhados.
