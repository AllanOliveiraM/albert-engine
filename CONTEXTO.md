# Albert Engine — contexto para continuidade

**Ponto de parada solicitado pelo Allan (17/10/2026):** tokens acabando; encerrar
em estado seguro para outro modelo. Última entrega: correção LF/CRLF do Windows,
badge e workflow de releases. Não iniciar outra feature antes de ler este estado.
Primeiro conferir o Actions do commit mais recente; depois retomar **Mimas**.

Registro das decisões da conversa, estado entregue e próximos passos. Leia junto
com `ROADMAP.md`, `docs/DEVELOPMENT.md`, `docs/LOCAL_SYSTEM.md` e `docs/RPC.md`.
Este registro acompanha a entrega das formas medidas de Fobos e Deimos.
**A etapa de fidelidade das luas ainda tem seis corpos pendentes.** Atualizar este
arquivo ao concluir essa etapa, conforme pedido do Allan, e a cada entrega relevante.

### Atualização — CI e releases (17/10/2026)

O Allan reportou falha Windows/MSVC no Actions: hash de `countries.geojson`.
`core.autocrlf` alterava LF para CRLF; `.gitattributes` agora usa `assets/** -text`
para preservar bytes. Não mudar hashes para aceitar a corrupção. Regressão de
checkout Windows em `tests/asset_checkout_fixture.py`, executada antes de configurar.
Badge **Build and tests** incluído no README, com a URL pedida pelo usuário.

O usuário também autorizou geração automática de distribuições no Actions:
`.github/workflows/release.yml`, tags `v[0-9]*` publicam após os dois builds/testes;
execução manual produz somente artefatos. Linux Debian 12/glibc 2.36+, Windows
MSVC x64. CI normal de branches/PRs permanece sem geração de dist.
ZIPs usam volumes até 1900 MiB, pois cada anexo GitHub deve ter menos de 2 GiB.
`scripts/package-release.py` escreve volumes por streaming, preserva modo Linux,
ZIP64 e gera SHA-256; nenhuma cópia integral do ZIP na RAM. Usuário baixa todas
as partes e extrai `.zip.001` com 7-Zip. Documentado em `docs/DEVELOPMENT.md`.
Testes adicionais: `asset_checkout` e `release_archive`; total Linux agora 25.
Releases públicas/tags não foram criadas manualmente nesta sessão.

Mimas: pesquisa começou, sem alterar inventário/pacote ainda. Originais obtidos
em `asset-packs/moons/sources/` (ignorados): `mimas_ver128q.tab` Gaskell/PDS,
99.846 vértices (98.306 usados), 196.608 placas, coordenadas km; e
`mimas-cassini-lpi.jpg` 6356×3178, Schenk/LPI/Cassini. LPI declara mapas como
propriedade pública com créditos em https://www.lpi.usra.edu/icy_moons/.
Essa imagem tem cores realçadas; não declarar cor natural. Registro/projeção
precisam ser conferidos antes da integração. `mimas-pia14926-fig1.jpg` baixado
é HTML redirecionado, **não é uma imagem válida**. Há atlas NASA GLB alternativo.
Retomar Mimas após fechar os ajustes de CI/releases.

Validação do corte de CI/releases: build incremental Linux e **25 CTests passaram**;
workflows passaram no actionlint 1.7.12. Testes pequenos verificam CRC, conteúdo,
modo executável, volumes e hashes. Teste adicional com payload esparso acima de
2 GiB gerou dois volumes ZIP64 e passou no `7z t` (7-Zip 26.02), sem criar uma
distribuição real. Fixture de releases também passou no Python 3.11 do container.
O container antigo `albert-toolchains` não tem Git: a regressão de checkout não
roda nele até instalar Git ou reconstruir a imagem. **Dockerfile.build agora
declara Git**, necessário para esse teste; o teste passou no host com Git.

**Ainda não validado:** execução integral do novo workflow em GitHub-hosted
Linux/Windows, build MSVC após a correção e publicação/upload de releases reais.
Não declarar Actions verde sem consultar o resultado remoto. Nenhuma tag ou
release pública foi criada, nenhum empacotamento real local foi gerado.
A publicação ocorre apenas em tags; a execução manual só cria artefatos.
Como estamos numa feature branch, o workflow manual precisa chegar à branch
padrão para aparecer na lista do GitHub. Não fazer merge/tag automaticamente.

## Como trabalhar com o Allan

- Conversar em português; informar progresso e próximo passo periodicamente.
- Priorizar velocidade, economia de tokens, execução robusta, desempenho e
  qualidade visual. Analisar o código existente antes de alterar; evitar abstrações
  desnecessárias. Código compacto é aceito, sem sacrificar correção.
- Continuar autonomamente o planejamento. Commit e push estão autorizados.
- Compilar e validar mudanças relevantes; não declarar testes ou precisão sem evidência.
- **Não gerar distribuições a cada alteração. Só gerar quando solicitado.**
- Não instalar o executável no sistema. A futura `dist/` deve ser portável,
  organizada, com executável na raiz para duplo clique, assets e dependências
  necessários; sem README, logs ou arquivos de desenvolvimento. Sem Microsoft Store.
- Linux e Windows são alvos. Documentar dependências e facilitar setup/build/run.
- Já houve falta de RAM e crash do PC. Usar **JOBS=1** nesta máquina e evitar
  builds/aplicações pesados simultâneos. Não mascarar o problema reduzindo fidelidade
  física; carregar detalhes sob demanda e liberar recursos fora de uso.
- A cauda de cometas é importante e tem prioridade alta, **depois do trabalho
  atual das luas**. Manter o pedido no roadmap.
- O usuário pediu exportação do contexto na raiz após terminar as luas. Este
  checkpoint já permite continuidade; atualizar contagens, entregas, testes e
  pendências ao concluir a fila restante, sem afirmar conclusão antecipada.

## Produto e requisitos preservados

Albert é um simulador astronômico C++20 com visualização threepp/OpenGL,
Dear ImGui, CMake, libcurl, ERFA, nlohmann/json e servidor gRPC C++.
Quer representar o Sistema Solar em escalas físicas, com assets de qualidade,
orientações/posições documentadas e navegação fluida. Efeitos artísticos devem
ser identificados separadamente dos dados astronômicos/observados.

O conteúdo é gerenciado dentro da engine. gRPC expõe essas ferramentas para
programas externos: criar/remover/focar objetos, câmera, geometria, assets,
órbitas, posição/velocidade/atitude e atualizações por streaming. O render cuida
de interpolação, matemática e animação. Exemplo desejado: cliente externo
simula lançamento do Starship e transmite estado/modelo; o usuário continua
livre para orbitar o foguete ancorado. Assistentes de IA poderão usar a mesma API.
Catálogos/provedores públicos e camadas geográficas/climáticas são extensões
futuras: verificar disponibilidade, termos e limites de cada fonte. Google/Earth
e Ventusky foram ideias, não integrações entregues nem licenças concedidas.

O usuário retirou a busca/importação online da UI. A seleção principal deve
continuar local e simples; integrações e objetos extras ficam disponíveis pela API.
Os objetos importantes vêm embarcados. Cinturões e Oort podem ser representações
estatísticas identificadas, sem catalogar cada partícula como objeto real.

## Estado entregue

- 43 corpos selecionáveis: Sol, oito planetas, 25 luas, cinco planetas anões,
  Vesta/Pallas e Halley/67P.
- Seletor por categorias, busca aproximada com acentos/transposições e desempate
  por distância heliocêntrica. Clique em corpos/labels de luas, cursor pointer,
  oclusão; labels ficam abaixo dos painéis e reduzem aglomeração com fade/histerese.
- Menu hambúrguer contextual, sem tooltips óbvios nem “Explorar o espaço”. ESC
  não encerra o programa. Restauração de zoom flutuante e fechamento do menu animados.
- Viagens de câmera com easing e progressão adaptados às distâncias, fases
  sobrepostas de afastar/alinha/aproxima e origem gráfica na câmera. Órbita lenta
  após chegar, cancelada por interação. Usuário é sensível a giros sem rumo,
  afastamentos excessivos, saltos e chegada que parece apenas aumentar uma esfera.
- Linhas de órbita: guia completa fina, passado contínuo/futuro tracejado com fade,
  cobertura subpixel, oclusão por objetos/relevo e escala moderada na aproximação.
- Loading animado com progresso geral/individual e transição para a cena;
  indicador de detalhes discreto sem deslocar o layout.
- Iluminação auxiliar por objeto, desligada por padrão e animada; deve clarear
  a parte não iluminada pelo Sol, independentemente de lado próximo/oculto lunar.
- Terra WGS84, atmosfera, cidades, fronteiras adaptadas ao relevo/zoom; Terra/Lua
  com imagens e DEM de alta resolução. Marte 32768×16384; Júpiter 6000×3000.
- Nuvens GIBS são opcionais, desligadas por padrão, com observações/atraso explícitos.
- Detalhes locais por região/zoom, preparados em worker, upload incremental e
  fade, sem exigir câmera parada; descarte ao afastar/trocar objeto. Evitar
  imagens inteiras em UHD residentes para todos os planetas.
- Cache persistente para dados dinâmicos, bootstrap com dados recentes e atualização
  em segundo plano; tamanho/limpeza/limite na UI. Imagens estáticas vêm no pacote.
- Tempo sincronizado por múltiplas fontes NTP; NTS/leap seconds completos pendentes.
- Eclipses Terra/Lua: disco solar finito, umbra/penumbra/antumbra e avermelhamento
  atmosférico aproximado documentado. Outros sistemas de eclipses ainda pendentes.
- Somente Saturno tem anéis renderizados. Outros anéis continuam prioritários.
- Formas artísticas próprias para Vesta, Pallas, Halley alongado, 67P bilobado,
  Hipérion e Febe; crateras/material procedural identificados como arte. Não são
  reconstruções medidas. Coma/caudas ainda não implementadas.
- Ícones de janela Linux/Windows e registro de atalho por usuário (`make desktop`).
  O comando não instala o executável. GPU física/Windows nativo ainda exigem validação.

## Luas — qualidade real e lacunas

19 das 25 luas têm imagens próprias, incluindo a Lua terrestre. As 18 adicionais:

| Grupo | Corpos / fonte atual |
| --- | --- |
| Júpiter | Io/Europa 4096×2048; Ganimedes/Calisto 2048×1024, NASA VTAD |
| Saturno | Encélado/Tétis/Dione/Reia/Jápeto 4096×2048, NASA VTAD |
| Urano | Ariel/Titânia/Oberon 2048×1024; Umbriel/Miranda 1024×512, cobertura parcial |
| Outros | Tritão 4096×2048 e Caronte 1264×632, cobertura parcial |
| Marte | Fobos/Deimos, mosaicos Viking 1440×720 e modelos DSK medidos |

Mapas estáticos não são atividade em tempo real nem albedo calibrado. Algumas
fontes têm iluminação embutida, lacunas ou áreas borradas. Registro das imagens
contra marcos IAU não foi independentemente validado. Não fabricar DEM a partir
do brilho da fotografia nem apresentar terreno inventado como medido.

Fobos: DLR/NAIF `phobos_2014_09_22.bds`, 137.439 vértices/274.874 placas,
frame IAU_PHOBOS (10021), raio máximo 13.923,082 m. Deimos: Thomas/Viking/ESA
`DEIMOS_K005_THO_V02.BDS`, 2.522 vértices/5.040 placas, frame IAU_DEIMOS
(10022), raio máximo 8.706,8 m. **Usar v0.2 de Deimos**, que corrige convenção
de longitude/rotação anterior. Fontes/hashes/créditos em `assets/moon-sources.json`.
Manter origem e escala medidas; raio nominal continua datum, sem renormalizar
essas malhas ao raio médio. Câmera usa raio máximo ×1,05. BVH seleciona placas
reais; LOD troca textura sem sobrepor malha esférica ao modelo.

Orientações IAU `pck00011` e termos periódicos/quadráticos foram testados contra
CSPICE. Isso **não torna as órbitas adicionais efemérides precisas**: 33 corpos
adicionais usam elementos Horizons embarcados de 16/10/2026 00:00 TDB e propagação
aproximada de dois corpos. Planetas e Lua preservam os provedores existentes.

## Próxima fila, por valor/viabilidade

1. **Mimas:** reprojetar atlas próprio de hemisférios; conferir longitude, polos,
   cobertura, orientação e possíveis modelos/relevo observados.
2. **Titã:** atmosfera e aparência visível próprias; não usar mapa infravermelho
   da superfície como fotografia em luz visível sem identificação.
3. **Hipérion/Febe:** imagens e formas observadas; substituir o procedural onde
   houver modelo confiável, preservando escala, picking e segurança da câmera.
4. **Proteu:** fontes disponíveis e cobertura limitada explicitadas.
5. **Nereida:** poucos dados; visual artisticamente plausível e identificado
   quando necessário, sem prometer fotorealismo observado indisponível.
6. Consolidar a etapa: registro cartográfico/limites, testes/visual/RAM e atualizar
   este contexto para a próxima IA. Melhorias adicionais de órbitas/relevo ficam explícitas.
7. Pequenos corpos, coma/caudas coerentes com geometria solar; anéis próprios de
   Júpiter/Urano/Netuno; elevar qualidade dos planetas/anões.

Também faltam: efemérides SPICE mais precisas para catálogo adicional, eclipses
generalizados, NTS, GLB com texturas/animação de peças, câmera RPC avançada,
camadas raster/GeoJSON/clima, data/replay/cenários. Alta prioridade transversal:
GPU física, Windows real, viagens extensas e limites de RAM/VRAM.
`ROADMAP.md` contém o backlog e histórico; seus critérios continuam válidos.

## Arquitetura e arquivos importantes

- `src/main.cpp`, `src/core/`: registro comum com IDs estáveis, SI/ICRF em double,
  estado temporal, fila limitada de comandos e bridge. Não modificar OpenGL em
  threads RPC. Render recebe snapshots/comandos pelo mecanismo existente.
- `src/rpc/`, `docs/RPC.md`: servidor, contratos e exemplos; escuta local por padrão.
- `src/catalog/`, provedores: catálogo e integrações compartilhados entre UI/API.
- `src/render/`: aplicação/UI/câmera; `visuals` carrega assets em worker;
  `regional` escolhe/prepara recortes; `solids` renderiza superfícies/anéis/LOD;
  `objects` sincroniza e seleciona; `shapes` decodifica malhas medidas e gera BVH.
- `surfaceBodies()/surfaceBody()` incluem superfícies/IAU das luas; `naturalBodies()`
  continua com sete planetas de provedores online. Não aumentar downloads de
  efemérides ao simplesmente adicionar uma superfície.
- `assets/moon-sources.json`, `scripts/import-planet-frames.py` geram
  `src/core/moon_frames.inc` e descritores físicos. Não editar gerados isoladamente.
- `scripts/prepare-moon-assets.py`, `moon_shapes.py`, `Dockerfile.assets`: autoria
  reproduzível com fontes fixadas por hash. SpicePy 8.3.0 somente na autoria,
  sem dependência SPICE no executável. `shape.bin` indexado little-endian:
  `ALBMSH1\0`, contagens uint32, xyz float32 em metros IAU, índices uint32.
- `scripts/reference-moon-shapes.py`: 48 raios independentes CSPICE/DSKX02 sobre
  originais; fixture em `tests/measured_shape_fixture.inc`.

## Assets e distribuição

Assets estáticos otimizados estão versionados em `assets/imagery/`, via Git LFS,
divididos apenas no Git e reunidos automaticamente no build em **um arquivo**.
A distribuição futura deve carregar a qualidade localmente, sem baixar imagens
estáticas em runtime. Esse empacotamento dificulta abertura casual; não é DRM.
Originais e intermediários ficam em `asset-packs/`, ignorados. Não espalhar cópias
de imagens pelo projeto; documentação/capas ficam em `docs/images/`.

Pacote adicional das 18 luas: **43.832.843 bytes**. Biblioteca reunida neste build:
**4.868.533.894 bytes** (~4,87 GB decimal). Não confundir disco com RAM/VRAM:
recortes são limitados e descartáveis; as duas malhas/BVH permanecem residentes.
Após mudar o inventário, regenerar pacote/hashes coerentes. Nunca versionar
originais enormes nem saltar a verificação de integridade para acelerar.

## Ambiente desta sessão e comandos

Repo: `/unreal/allan/albert-engine`, zsh. Branch
`allanoliveiram/feat/initialize-albert-engine`; remote SSH
`git@github.com:AllanOliveiraM/albert-engine.git`. Verificar `git status` antes de
continuar e preservar alterações do usuário. Consultar `git log` para hashes atuais.

```sh
make build JOBS=1
make run JOBS=1
ctest --test-dir build/linux --output-on-failure -j1
# Regeneração opcional das luas, Docker declarado no projeto:
make assets-moons
bash scripts/prepare-assets.sh --moons --offline
```

Build local já existe; recompilar incrementalmente. Caminho CMake atual
`ALBERT_ASSET_PACK=/unreal/allan/albert-engine/build/linux/imagery/visuals-v2.pak`.
Preservá-lo; não voltar acidentalmente a uma biblioteca antiga. Não rodar montagem
Linux/Windows simultaneamente sobre o mesmo `.pak.tmp` compartilhado.

Validação isolada desta sessão usou Docker `albert-toolchains`, mount `/work`,
Xvfb `:100` (TCP `172.17.0.2:100`), llvmpipe e MinGW em `build/m0-windows`.
Endereços/container são detalhes locais, confirmar disponibilidade antes de usar.
Não automatizar nem capturar desktop pessoal; screenshots foram apenas da janela
de teste iniciada por nós no display isolado. Encerrar somente processos próprios.

```sh
# Gráficos Linux no display de teste, sem Wayland:
env -u WAYLAND_DISPLAY DISPLAY=172.17.0.2:100 LIBGL_ALWAYS_SOFTWARE=1 \
  LP_NUM_THREADS=2 build/linux/tests/visual_test --graphics
# Geometria medida Linux:
build/linux/tests/shape_test build/linux/imagery/visuals-v2.pak
# Cross build já configurado:
docker exec albert-toolchains cmake --build /work/build/m0-windows \
  --parallel 1 --target render visual_test bodies_test shape_test
```

Wine usa `build/m0-wine-prefix`. Para gráficos, definir `ALBERT_PACK_PATH` com
caminho Windows `Z:/unreal/allan/albert-engine/build/linux/imagery/visuals-v2.pak`;
`shape_test.exe` recebe esse caminho como argumento. Ver `docs/DEVELOPMENT.md`.
Autoria Docker deve usar `--user "$(id -u):$(id -g)"` para evitar arquivos root.

O aplicativo usa GPU automaticamente via OpenGL quando o driver oferece contexto
acelerado. A seleção entre integrada/dedicada fica a cargo do sistema/driver.
**LIBGL_ALWAYS_SOFTWARE=1 foi usado só nos testes isolados**, não na execução normal.
CPU/RAM continuam envolvidos em leitura, descompactação, preparação de geometria.

## Última validação efetiva

- 23 testes CTest Linux passaram, incluindo `measured_shapes`.
- Geometria: contagens/escala/normais/UVs, entradas malformadas e 48 interseções
  contra CSPICE independentes, tolerância de 5 cm.
- OpenGL Linux e Windows/Wine passaram: upload incremental, fade, picking pela
  malha, ausência de esfera sobre modelo medido, liberação de texturas e regressões
  existentes de terreno/órbitas/nuvens/eclipses/objetos.
- Build Linux e cross MinGW completos; teste científico Windows/Wine passou.
- Programa real no Xvfb: foco/zoom/detalhes de Fobos e Deimos, clamp de câmera e
  retorno à Terra. Pico de RSS 1.055.444 kB (~1,01 GiB) em 600×480 com llvmpipe.
  Não é orçamento máximo nem medição de VRAM ou performance de GPU física.
- Evidências locais em `build/measured-shapes-*.log` e imagens correspondentes,
  ignoradas pelo Git. Nenhuma distribuição gerada nesta entrega.

Próxima ação concreta: conferir árvore/commit, depois iniciar **Mimas**, mantendo
o inventário, pacote, descritores, testes e documentação sincronizados. Ao encerrar
a etapa das seis luas restantes, atualizar este arquivo antes de passar o trabalho.
