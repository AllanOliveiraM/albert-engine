# Albert Engine — contexto para continuidade

**Estado em 17/10/2026 (manhã), continuação por Claude Opus 5.5.** Entregues nesta
sessão: correções do Windows no CI, cache de LFS/compilação no CI, **Mimas**
completo, o **download dos assets por níveis** (versão leve), correções da revisão
do Codex, **Titã** (névoa visível + superfície em 938 nm), **Febe** (forma Gaskell +
mapa Cassini com lacunas preenchidas) e **Hipérion** (forma Thomas, cor uniforme
medida, orientação ilustrativa ancorada em 2005); repasse do Codex sem achados novos;
**Proteu** (forma de Stooke, cinza neutro medido) e **Nereida** (esfera com albedo
medido e aviso). **Etapa das luas concluída**, com repasse do Codex sem achados novos.
Em andamento: pequenos corpos medidos (Alta 2) — **67P** entregue (forma OSIRIS/Rosetta,
cor medida). Próximo: Halley → Pallas → Vesta; depois coma/caudas.

Registro das decisões da conversa, estado entregue e próximos passos. Leia junto
com `ROADMAP.md`, `docs/DEVELOPMENT.md`, `docs/ASSETS.md`, `docs/LOCAL_SYSTEM.md`
e `docs/RPC.md`. **A etapa de fidelidade das 25 luas está concluída** (limites de cada uma no inventário).
Atualizar este arquivo ao concluir essa etapa, conforme pedido do Allan, e a cada
entrega relevante.

### CI, releases e cache (17/10/2026)

O Windows/MSVC **nunca tinha compilado o projeto** (os 22 runs anteriores falharam).
Bloqueios removidos em sequência, cada um escondido atrás do anterior: CRLF em
`countries.geojson` (`assets/** -text`); `Math.Min(Int32, Int64)` em
`scripts/assemble-assets.ps1` com o pacote de 2,86 GB; macro `max` do `windows.h`
puxado pelo `curl.h` em `src/net/download.cpp` (`NOMINMAX`); Abseil forçando o CRT
dinâmico (`ABSL_MSVC_STATIC_RUNTIME ON`, LNK2038 no `protoc`); Windows PowerShell 5.1
sem `Get-FileHash` quando iniciado a partir do PowerShell 7 (`make.cmd` limpa
`PSModulePath`; montagem usa SHA-256 do .NET — afeta também usuários no Windows
Terminal); bit de execução do `AlbertEngine` perdido ao empacotar no Windows.
No run de `da906d2` o MSVC **compilou e linkou tudo em 8 min** (cache de `_deps`) e
passou 25/26 testes. **Primeiro CI totalmente verde, Linux e Windows/MSVC:** `0bcd4fa`
(run 39019567564; Linux 5 min, Windows 12 min com build e todos os testes).
Montagem PowerShell validada em Docker com bytes idênticos ao Linux. Não mudar hashes.

**Banda LFS:** downloads LFS do Actions contam na cota da conta (10 GiB/mês no
plano Free, bloqueio ao estourar). `build.yml`: checkout sem LFS, chave pelos OIDs,
um cache compartilhado Linux/Windows (`enableCrossOsArchive`, ~4,8 GB), `git lfs pull`
só do que falta, e apaga `.git/lfs/objects` após copiar. ccache no Linux e
`build/windows/_deps` no Windows são restaurados/salvos explicitamente e **salvos
mesmo quando o build falha** (`!cancelled()`), para uma falha não custar outra
compilação de gRPC. `concurrency` cancela runs antigos da mesma branch: **um push
cancela o Windows em andamento** e esse cache não é salvo; quando o Windows estiver
compilando dependências pela primeira vez, esperar terminar antes de enviar.
Resultado medido: job Linux de 20–42 min para **5 min** (LFS do cache, build 64 s).
`release.yml` compila do zero, apaga `.git/lfs/objects` após o checkout e publica
completa + leve; containers só quando `assets-<id>` não existe completa. Releases/tags não
foram criadas. Não fazer merge/tag automaticamente.

Não declarar Windows verde sem consultar o Actions do último commit.

**Erros do CI sem login:** os builds gravam a saída (`build-windows.log`,
`build-linux.log`) e, se falharem, `scripts/ci-errors.py` publica erros únicos de
compilador, linker, CMake e scripts PowerShell como anotações, legíveis pela API
pública: `GET /repos/<repo>/check-runs/<job id>/annotations`. Falhas de teste ainda
exigem o log (`ctest --output-on-failure`). `gh` não está instalado e a API pública
tem 60 req/h; logs brutos: abrir `https://github.com/<repo>/commit/<sha completo>/checks/<job>/logs`
no navegador do Allan e filtrar com `fetch(location.href)`; o filtro de privacidade do
navegador bloqueia saídas com aparência de URL/cookie, então remover esses caracteres.

### Revisão periódica pelo Codex

Fluxo pedido pelo Allan: ao concluir etapa relevante ou antes de release (nunca por
alteração pequena nem em paralelo), na raiz:

```sh
codex exec --sandbox read-only -c 'approval_policy="never"' -o build/revisao-codex.txt \
  resume 01a131bd-6526-7473-865a-137544a04ecf "<prompt de revisão do Allan>"
```

Ler `build/revisao-codex.txt`, conferir cada achado no código, corrigir os confirmados
com regressões, registrar justificativa técnica quando discordar e atualizar este
arquivo antes do próximo repasse.

Revisão de 17/10/2026 (recebida do Allan), três achados, **todos confirmados e
corrigidos em `3356cb5`**:
1. Alta — container no disco aceito só pelo tamanho; byte alterado contava como
   completo. Agora `<arquivo>.verified` (SHA-256, tamanho, mtime) é gravado após
   cada verificação; sem registro válido o container é re-hasheado e substituído
   se divergir; a biblioteca só abre containers registrados. Limite conhecido:
   alteração que preserve tamanho e mtime só é pega na leitura (SHA por bloco).
2. Alta — release leve podia sair com `assets-<id>` ausente ou parcial. Agora os
   catálogos Linux/Windows têm de ser idênticos, release parcial é refeita com
   `--clobber`, e `scripts/asset-release.py` confirma todos os containers antes da
   release versionada.
3. Média — após trocar o foco, o primeiro nível mais fino não recarregava o detalhe.
   `DetailRefresh` registra o nível do corpo novo na troca de foco.
Regressões: cópia corrompida rebaixada sem rebaixar as íntegras (servidor confere),
holder recusando container sem registro, refresh após troca de foco, completude
da release de assets; mutação (confiar só no tamanho) faz o teste falhar.

Repasse do Codex de 17/10/2026 (sessão retomada, `build/revisao-codex.txt`): os três
achados acima reavaliados como corrigidos; um novo, **confirmado e corrigido**:
P1 — release `assets-<id>` em rascunho passava como completa (anexos de rascunho não
são públicos). `scripts/asset-release.py` exige `isDraft: false` (ausente conta como
rascunho) e o job de publicação publica um rascunho existente, sem marcá-lo como
latest, antes de verificar. Regressão em `tests/release_fixture.py`. O Codex rodou
`bodies_test`, `shape_test`, CPU do `visual_test` e actionlint; não CTest completo.
A sessão do Codex fica bloqueada ("active writer") se a conversa estiver aberta na
extensão do VS Code; nesse caso pedir ao Allan para liberá-la, não abrir sessão nova.

Repasse de 17/10/2026 após Febe e Hipérion (`0bcd4fa..1f97e52`, árvore limpa): **nenhum
defeito novo confirmado**; a proteção contra release em rascunho foi reavaliada como
corrigida (o Codex reproduziu a rejeição de rascunho, estado ausente e anexos
incompletos). O Codex rodou `bodies_test`, `shape_test` (120 interseções), CPU do
`visual_test`, `anchor-orientation.py hyperion` (latitude subsolar a 0,18°), hashes de
`cmake/Assets.cmake` e actionlint; não recompilou nem validou OpenGL, Windows nativo ou
o CI do novo HEAD. CI de `028f24f` (Febe) verde em Linux e Windows.

Repasse ao fim da etapa das luas (`1f97e52..f317697`, árvore limpa): **nenhum defeito
novo confirmado**; pendências anteriores continuam corrigidas. O Codex rodou
`bodies_test`, `shape_test` (144 interseções), CPU do `visual_test`, conferiu SHA,
escala, limites, fechamento e orientação dos 5.040 triângulos de Proteu, o pacote das
luas, `Assets.cmake`, actionlint e `git diff --check`; sem OpenGL, Windows, downloads
reais ou RAM/VRAM.

### Download por níveis (entregue em 17/10/2026)

Decisão do Allan: versão leve baixa tudo, por **níveis de resolução**, com tela de
loading só para o essencial e o resto em segundo plano, indicador minimalista no
canto, retentativa e continuação; zoom limitado ao que chegou, nunca imagem
inventada. `scripts/split-asset-pack.py` gera `essential.pak` (68 MB com Febe e Hipérion) + 18
containers `<corpo>-<nível>.pak`/`moons-<nível>.pak` e `catalog.json`;
`AssetPack` indexa vários arquivos e limita cada camada ao maior nível completo;
`AssetDownloads` (`src/render/asset_downloads.*`) baixa da release `assets-<id>`
(`ALBERT_ASSET_BASE_URL` para testes) para `assets/packs` ou `data/packs`.
`scripts/light-dist.py` deriva a pasta leve. Testes: `asset_containers`,
`asset_downloads` (servidor local com 503, corte, Range ignorado, corrupção,
redirect, lentidão, preempção, reserva de disco e cancelamento) e `release_archive`.
App real testado contra servidor local (scripts em `build/light-*.py`, ignorados).
Pendente: Windows nativo e uma release real `assets-<id>` no GitHub.

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
- **Perguntas e decisões pendentes vão na mensagem final do turno**, completas e
  estruturadas. O Allan acompanha de outro dispositivo e perde texto intermediário.
- RAM: o `cpptools` do VS Code chegou a 6,1 GB de RSS nesta máquina (o
  `C_Cpp.maxMemory` não limita o processo principal). O Allan liberou ao agir no
  VS Code; ele não usa a extensão C++ e pode desinstalá-la. Conferir `free -m`
  antes de builds/análises pesadas.

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

- 43 corpos selecionáveis: Sol, oito planetas, 25 luas (21 com imagens próprias), cinco planetas anões,
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
  em segundo plano; tamanho/limpeza/limite na UI. Imagens estáticas vêm no pacote
  completo ou são baixadas por níveis na versão leve (ver seção acima).
- Tempo sincronizado por múltiplas fontes NTP; NTS/leap seconds completos pendentes.
- Eclipses Terra/Lua: disco solar finito, umbra/penumbra/antumbra e avermelhamento
  atmosférico aproximado documentado. Outros sistemas de eclipses ainda pendentes.
- Somente Saturno tem anéis renderizados. Outros anéis continuam prioritários.
- Formas artísticas próprias para Vesta, Pallas, Halley alongado e 67P bilobado;
  crateras/material procedural identificados como arte. Fobos, Deimos, Febe, Hipérion,
  Proteu e a 67P voltam à rocha artística só se a biblioteca não tiver nenhum asset deles. Não são
  reconstruções medidas. Coma/caudas ainda não implementadas.
- Ícones de janela Linux/Windows e registro de atalho por usuário (`make desktop`).
  O comando não instala o executável. GPU física/Windows nativo ainda exigem validação.

## Luas — qualidade real e lacunas

22 das 25 luas têm imagens próprias, incluindo a Lua terrestre; Hipérion e Proteu têm
forma medida sem mapa. As 21 adicionais com mapa:

| Grupo | Corpos / fonte atual |
| --- | --- |
| Júpiter | Io/Europa 4096×2048; Ganimedes/Calisto 2048×1024, NASA VTAD |
| Saturno | Mimas 6356×3178 Cassini PIA18437 + forma Gaskell; Titã névoa visível + 938 nm PIA22770 5760×2880; Febe PIA07775 2880×1440 + forma Gaskell; Encélado/Tétis/Dione/Reia/Jápeto 4096×2048, NASA VTAD |
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

Mimas: forma Gaskell/PDS Q=256 (`mimas_ver256q.tab`), 393.218 vértices usados
e 786.432 placas, raio máximo 210.780,893 m; descartados só vértices duplicados do
cubo ICQ sem uso. Mosaico PIA18437 (NASA/JPL/SSI/LPI, Schenk), cores realçadas
IR–verde–UV (não é cor natural). **O mosaico publicado não estava registrado:**
longitude −3,85° e latitude planetográfica, tan φ' = (207,8/190,6)² tan φ. O campo
`projection` do inventário faz o gerador reamostrar (cúbica separável). Medido pela
correlação com sombreamento sintético da malha em 84 regiões (0,04 → 0,51, 83/84
melhores) e conferido em 21 crateras do Gazetteer; `scripts/register-moon-map.py
mimas` reproduz. A malha usa W0 337,46° e o Albert usa pck00011 (333,46°): longitudes
da superfície batem com o Gazetteer; a fase inercial pode diferir ~4° (documentado).
Polos de Gaskell ficam a 3–5 m do eixo em Mimas e a 47–133 m em Febe. `decodeShape`
trata como polo o vértice com distância ao eixo menor que 1/4 da dos outros dois
vértices da placa: recebe a longitude da placa e fica fora do cálculo da emenda (a
tolerância fixa de 1e-4 falhava em Febe; regressão sintética em `shape_test`).

Febe: forma Gaskell/PDS Q=128 (`phoebe_ver128q.tab`, 2013), 98.306 vértices usados
e 196.608 placas, raio máximo 122.276,613 m; polo da fonte 0,08° diferente do
pck00011 em declinação, mesmo W0/rotação. Mapa PIA07775 (NASA/JPL/SSI, dez/2005),
cinza, 3228×1900 com moldura: `projection.crop` recorta 2880×1440 (8 px/grau,
oeste 180–0–180 = leste −180…180). Sem observação ao norte de ~60° N e ao sul de
81° S (faixa uniforme de valor 5) e ruído quase preto nas lacunas:
`projection.no_data` (valor 0, `dark_max` 12 conectado ao preenchimento, `south_of_lat`
−81, `fringe_px` 2) marca 27% da grade, preenchida com a média local (~2°) dissolvida
no cinza mediano. Manchas cinza 137–139 no sul são preenchimento dos autores (mantidas).
Registro: correlação 0,109 em 58 regiões; nenhum modelo da varredura é melhor por mais
de dois erros-padrão (melhor: k 1,00, +2°, 0,118), então a grade publicada foi mantida
(incerteza ~2°, 4 km). O critério do `register-moon-map.py` passou de "melhor modelo
exato" para esse teste pareado; Mimas continua aprovado (0,514, 83/84). O ajuste de
bordas de crateras do Gazetteer não serve em Febe (MAD ~5°: crateras de 30–100 km num
corpo de 213 km); o script agora imprime a dispersão.

Hipérion: placas de Thomas (`hyperion_30k_plt.tab`, PDS doi:10.26033/ewy3-jy61),
14.636 vértices/29.268 placas, índices a partir de zero, km; raio máximo 184.616,033 m,
volume equivalente a 136,25 km; eixo longo em +Z; todas as placas voltadas para fora da
origem. `kind: "uniform"`: cor única (0,352; 0,293; 0,220) lineares = cromaticidade do
disco em PIA08240 (cor natural) com luminância 0,3 (estimativas 0,2–0,44). **O modelo
VTAD (`hyperion.glb`) foi descartado:** ajuste por Gauss-Newton em resíduos radiais (24
partidas, escala 0,896) só reproduz a forma geral (mediana 4,4 km, RMS 8,2 km;
espelhado não melhora) e nem relevo nem textura VTAD correlacionam com o relevo de
Thomas (0,02–0,05; autoteste 0,999). Código da avaliação não versionado (descrito aqui).
Orientação ilustrativa (`orientation` no inventário; `scripts/anchor-orientation.py`):
polo 299,8°/58,7° e 72°/dia (solução 2005/268 17:20–269/02:18 de Bauer & Thomas 2007,
que dá 11,29° de latitude subsolar contra 11,11° do modelo) e W0 = 200,4161° ancorado
no ponto subsolar de N1506385183 (longitudes do sobrevoo reproduzidas em 0,01°).
Direção do Sol pelos elementos de Standish, conferida pelas outras soluções publicadas
(−13,98/26,27/−43,66 contra −14,1/27,0/−43,7). Fixture CSPICE com essas constantes num
PCK de texto (época 2440000 trocada por 2005-09-26: 72°/dia × 11.545 d = voltas inteiras).
Em fase baixa Hipérion parece liso: falta o contraste de albedo; projetar imagens Cassini
sobre a forma exigiria kernels SPICE de apontamento (trabalho futuro).

Proteu: grade de Stooke (`n8proteus.tab`, PDS4 `small_bodies.stooke.shape-models`,
doi:10.26033/yt84-5y91): raio a cada 5°, km, longitude **oeste**, 0° voltado para
Netuno (como IAU_PROTEUS), origem fora do centro de figura (mantida). Formato
`stooke-radius-grid`: triangulação só dos pontos publicados (anéis de quadriláteros +
leques polares), 2.522 vértices/5.040 placas, malha fechada, estrelada, raio máximo
232,5 km, volume equivalente 201,0 km (Karkoschka 2003: 210 ± 7). `reference-moon-shapes`
triangula a grade de forma independente. Sem mapa (Voyager viu parte; nenhum mosaico
ou modelo 3D NASA): `kind: uniform` cinza neutro 0,096 (cor plana 0,4–0,6 µm, Thomas e
Veverka 1991; Voyager sem oposição ~0,06). Orientação pck00011 com termos de Netuno.

Nereida: sem forma nem imagem resolvida (Voyager a ~4,7 milhões de km). Esfera do raio
de catálogo (170 km; Kiss et al. 2016, doi:10.1093/mnras/stw081: D 335–345 km, pV
0,25 ± 0,02 por NEATM, cor perto do grupo neutro-escuro; curvas de luz admitem ≤ 1,3:1),
`kind: uniform` cinza 0,25, sem `shape_source`. `orientation` ilustrativa sem âncora:
período K2 11,594 h (745,213°/dia), polo de Netuno, W0 0; sem efeito visível. Fixture
CSPICE com essas constantes num PCK de texto.

67P: DSK `ROS_CG_K195_OSPCLPS_N_V1.BDS` (OSIRIS SPC LAM-PSI, corpo 1000012, frame
−1000012000; ESA lenta, baixado do espelho NAIF, primeiro MiB conferido com a ESA).
13 aletas de espessura zero (placa + inversa, vértice exclusivo) removidas por
`moon_shapes.without_flaps` (falha se a duplicata não for invertida; no-op em Fobos e
Deimos): 97.323 vértices/194.642 placas, raio máximo 2.613,405 m. Duas placas são
atravessadas pelo eixo polar sem vértice próximo (polo sul e "pescoço"): `decodeShape`
dá a elas uma única longitude (regressão sintética falha no código anterior). Cor
(0,0625; 0,0586; 0,0531) de Fornasier et al. 2015 (6,5% a 649 nm, 11%/100 nm).
Orientação pck00011 BODY1000012 (época Rosetta); fixture via `tipbod` transposto (sem
frame IAU nomeado). `moon-sources.json` passou a cobrir pequenos corpos (nome
histórico): `bodies_test` aceita categoria ≠ Planet e o download prioriza "moons" para
qualquer superfície local. No `visual_test`, o plano próximo da câmera passou a
acompanhar o raio (o fixo de 6,4 km cortava a 67P).
A mesma técnica pode validar os mosaicos Viking de Fobos/Deimos (ainda não feito).

Orientações IAU `pck00011` e termos periódicos/quadráticos foram testados contra
CSPICE. Isso **não torna as órbitas adicionais efemérides precisas**: 33 corpos
adicionais usam elementos Horizons embarcados de 16/10/2026 00:00 TDB e propagação
aproximada de dois corpos. Planetas e Lua preservam os provedores existentes.

## Próxima fila, por valor/viabilidade

1. ~~Download dos assets por níveis~~ — entregue (seção acima).
2. ~~Titã~~ — entregue: padrão em luz visível (cor de PIA06230, albedo 0,22, névoa
   principal H = 60 km e camada alta a 400 km com Henyey-Greenstein g = 0,65); opção
   "Superfície em infravermelho (938 nm)" com PIA22770, registro conferido em 12
   feições. `NaturalBody::hazeAlbedo/hazeAltitudeMeters`, `SolidBodies::showSurface`.
3. ~~Hipérion e Febe~~ — entregues em 17/10/2026 (ver acima).
4. ~~Proteu~~ — entregue em 17/10/2026 (forma de Stooke, cor neutra uniforme).
5. ~~Nereida~~ — entregue em 17/10/2026 (esfera com albedo medido e aviso).
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
- `scripts/reference-moon-shapes.py`: 168 raios independentes (CSPICE/DSKX02 nos
  DSKs, float64 com parser próprio na tabela Gaskell); `tests/measured_shape_fixture.inc`.
- `scripts/moon_shapes.py` exporta DSK (SpicePy) e Gaskell vértice-faceta (só NumPy).
  Inventário aceita `projection` (registro de mapas publicados) e `notice` (aviso
  da UI gerado em `NaturalBody::notice`). `scripts/register-moon-map.py` mede registro.
- `tests/moon_frames_fixture.inc` não tem gerador versionado: entradas novas vêm de
  SpicePy `pxform('IAU_<CORPO>','J2000',(jd-2451545)*86400)` com pck00011.

## Assets e distribuição

Assets estáticos otimizados estão versionados em `assets/imagery/`, via Git LFS,
divididos apenas no Git e reunidos automaticamente no build em **um arquivo**.
A versão completa carrega a qualidade localmente, sem baixar imagens estáticas
em runtime; a leve baixa os mesmos bytes por níveis (decisão de 17/10/2026).
Esse empacotamento dificulta abertura casual; não é DRM.
Originais e intermediários ficam em `asset-packs/`, ignorados. Não espalhar cópias
de imagens pelo projeto; documentação/capas ficam em `docs/images/`.

Pacote adicional das 24 luas e da 67P: **77.482.328 bytes**. Biblioteca reunida neste build:
**4.902.183.379 bytes** (~4,90 GB decimal); essencial da versão leve: 71,3 MB (era 63,3; as malhas de Febe, Hipérion, Proteu e 67P vão no essencial). Não confundir disco com RAM/VRAM:
recortes são limitados e descartáveis; as sete malhas/BVH medidas permanecem residentes.
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

- Build Linux incremental (JOBS=1) e **25 CTests passaram**, incluindo
  `measured_shapes` com Mimas (72 interseções; tolerância 5 cm em Fobos/Deimos e
  0,5 m em Mimas, coerente com float32 a 210 km) e `natural_bodies` com Mimas.
- `tests/moon_assets_fixture.py` cobre a reprojeção (identidade, borda 0–360,
  latitude planetográfica e sinal do deslocamento de longitude).
- OpenGL no Xvfb (`visual_test --graphics`) passou com Mimas na malha medida:
  render, picking BVH, upload com fade, geometria preservada e descarte.
- Programa real no Xvfb, 800×600, llvmpipe: foco/zoom/detalhe de Mimas, câmera
  fora da malha (raio máximo ×1,05) e aviso da fonte no painel. Pico de RSS
  1,29 GB; não é medição de VRAM nem de GPU física. Capturas em
  `build/mimas-preview-*.png` e registro em `build/mimas-registration-*.png`.
- Cross-build MinGW (`render`, `visual_test`, `bodies_test`, `shape_test`) e Wine:
  geometria (72 interseções), orientação contra CSPICE e OpenGL passaram.
- Nenhuma distribuição gerada nesta entrega.

- Downloader: 27 CTests Linux; `asset_containers`/`asset_downloads` também via Wine
  (MinGW). App real em modo leve: cena em 16,9 s, 19 containers em 235 s a 25 MB/s,
  foco à frente, retomada com Range após reinício, pico RSS ~1,06–1,20 GB (llvmpipe).

- Titã: 27 CTests Linux; OpenGL (névoa cobre o mapa, modo superfície o revela) no
  Linux e no Wine; `bodies_test` contra CSPICE no Wine; app real no Xvfb em fase
  moderada e contra o Sol (`build/titan-preview-*.png`).

- Febe: 27 CTests Linux (`measured_shapes` com 96 interseções, `natural_bodies` com
  IAU_PHOEBE em quatro épocas, regressão do polo fora do eixo); `moon_assets_fixture.py`
  com recorte/lacunas; OpenGL no Xvfb; MinGW + Wine (`shape_test`, `bodies_test`,
  `visual_test --graphics`); app real no Xvfb com foco/zoom/detalhe, câmera fora da
  malha, pico RSS 1,24 GB (`build/phoebe-preview-*.png`). A regressão do polo falha no
  `shapes.cpp` anterior.

- Hipérion: 27 CTests Linux (120 interseções, IAU_HYPERION ilustrativo contra CSPICE,
  fallback para rocha sem assets, que falha no `objects.cpp` anterior); OpenGL no Xvfb;
  MinGW + Wine (`shape_test`, `bodies_test`, `visual_test` com e sem gráficos); app real
  no Xvfb com foco/zoom/detalhe, câmera fora da malha, pico RSS 1,24 GB
  (`build/hyperion-preview-*.png`).

- Proteu: 27 CTests Linux (144 interseções; IAU_PROTEUS contra CSPICE em quatro
  épocas); OpenGL no Xvfb; MinGW + Wine (`shape_test`, `bodies_test`, `visual_test`
  com e sem gráficos); app real no Xvfb, câmera fora da malha, pico RSS 1,25 GB
  (`build/proteus-preview-*.png`).

- Nereida: 27 CTests Linux (IAU_NEREID ilustrativo contra CSPICE; laço das luas do
  OpenGL inclui Nereida); OpenGL no Xvfb; MinGW + Wine (`bodies_test`, `shape_test`,
  `visual_test --graphics`); app real no Xvfb, pico RSS 1,27 GB
  (`build/nereid-preview-*.png`).

- 67P: 27 CTests Linux (168 interseções; frame contra CSPICE); OpenGL no Xvfb; MinGW +
  Wine (`bodies_test`, `shape_test`, `visual_test` com e sem gráficos); app real no Xvfb,
  câmera fora da malha, pico RSS 1,21 GB (`build/comet67p-preview-*.png`).

Próxima ação concreta: **Halley** (Stooke, `1682q1halley.tab` no pacote de Proteu;
longitude **leste** para cometas na V2.0; sem modelo IAU de rotação: período/eixo
publicados de Giotto/Vega a pesquisar), **Pallas** (sem sonda: elipsoide do pck00011 +
cor medida) e **Vesta** (Dawn: forma Gaskell/PDS e mosaico FC da USGS, registrar com
`register-moon-map.py`). Depois coma/caudas com atividade coerente com a distância
heliocêntrica (Halley perto do afélio em 2026 não deve ter coma).
