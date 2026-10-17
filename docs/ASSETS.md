# Biblioteca visual incluída

A biblioteca completa está versionada em **`assets/imagery/`** usando Git LFS.
São onze partes de até 1 GiB, reunidas automaticamente em `visuals.pak` durante
o build, com verificação SHA-256 de cada parte e do resultado completo.
O arquivo final tem **5.451.393.728 bytes (5,45 GB / 5,08 GiB)**: biblioteca
original de 2,86 GB, Mercúrio de 1,69 GB, Sol de 276 MB, Plutão de 227 MB, Ceres de
167 MB, Vesta de 106 MB, luas e pequenos corpos de 77 MB e Caronte de 49 MB, com
índice de coleção.
As partes preservam exatamente os mesmos bytes; não reduzem a qualidade.

Quem clona o projeto com Git LFS não precisa gerar as imagens novamente.
`setup-linux.sh` e `make.cmd setup` configuram Git LFS e baixam as partes.
Se o Git deixou ponteiros no lugar dos dados, execute `git lfs pull`.

## Recriar a partir das fontes

O download e a geração estão declarados no próprio projeto. Os originais e os
intermediários ficam em `asset-packs/`, ignorada pelo Git. Essa preparação
avançada usa Docker, separadamente da compilação normal.

Com Docker instalado e em execução, reserve pelo menos **25 GiB livres**:

```sh
# Linux — baixar e processar novamente
bash scripts/prepare-assets.sh --rebuild
```

```powershell
# Windows, Docker usando containers Linux
docker build -t albert-assets-builder -f scripts/Dockerfile.assets scripts
docker run --rm --init -v "${PWD}:/work" -w /work albert-assets-builder
```

O comando baixa os originais, converte as projeções, cria os blocos otimizados
e os reúne no arquivo final. A geração também atualiza as partes LFS em
`assets/imagery/` e seu manifesto. `make assets` apenas informa que a biblioteca
versionada está pronta; use os comandos acima para regenerá-la.
Pode demorar: são imagens globais com bilhões de pixels. Downloads interrompidos
usam arquivos `.part`; execute novamente para continuar. Blocos já concluídos são
reutilizados. O processamento usa GDAL e Pillow dentro do container declarado em
`scripts/Dockerfile.assets`, com memória limitada para os buffers de imagem.

## Arquivo incluído no programa

Ao terminar, o comando valida os blocos e cria **`asset-packs/visuals.pak`**.
As partes são atualizadas em `assets/imagery/`. O build as reúne em
`build/<ambiente>/imagery/visuals.pak`. `make dist` ou `.\make.cmd dist`
copia esse arquivo para `dist/<sistema>/assets/visuals.pak`. As duas versões usam o mesmo pacote.
Não envie `sources`, `work` ou `public` junto do programa: são originais e
intermediários usados somente para preparar/recriar o pacote.

```text
assets/imagery/              versionado com Git LFS
  manifest.json             ordem, tamanho e hashes
  visuals-000.pakpart
  visuals-001.pakpart
  visuals-002.pakpart

asset-packs/                 fora do Git
  sources/                  originais e seus SHA-256
  work/                     projeções virtuais e estado
  public/                   blocos intermediários e manifesto
  visuals.pak               pacote final
  visuals.pak.json           tamanho e SHA-256 do pacote, para conferência

dist/linux/ ou dist/windows/
  AlbertEngine[.exe]
  assets/
    visuals.pak
    ...dados auxiliares...
  licenses/
```

O programa lê diretamente os blocos do `.pak`, sem extrair o conteúdo nem
carregar tudo em memória. As imagens já estão comprimidas em JPEG/PNG;
o contêiner acrescenta um índice compacto com offsets de 64 bits.
Não possui criptografia: evita a abertura casual das imagens por duplo clique,
mas não pretende impedir extração por ferramentas especializadas.

O cabeçalho contém os 8 bytes `ALBRTPK1`, seguidos do tamanho do índice JSON
em inteiro little-endian de 64 bits. O índice declara camadas, dimensões,
metadados de origem e cada payload (`path`, `offset`, `bytes`, `sha256`).
Offsets são relativos ao início dos payloads, após o cabeçalho e o índice.
`scripts/pack-assets.py` reproduz o empacotamento; arquivos são publicados
com escrita temporária e renomeação apenas depois de verificar os hashes.

## Qualidade e formato

| Camada | Cobertura / limite da fonte | Saída |
| --- | --- | --- |
| Terra, cor | Global 86400×43200, cerca de 500 m/pixel no equador; outubro de 2004 | JPEG qualidade 94, sem subamostragem de cor |
| Terra, cidades | Global 86400×43200, cerca de 500 m/pixel; Black Marble 2016 | PNG sem perdas |
| Lua, superfície | Global 108000×54000, cerca de 101 m/pixel; LROC WAC | JPEG qualidade 94 |
| Terra, elevação | GEBCO08 21600×10800, cerca de 1,9 km/pixel; 8 bits | PNG, sem perdas adicionais ao JPEG original |
| Lua, elevação | LOLA 23040×11520, cerca de 474 m/pixel; 16 bits, passos de 0,5 m | PNG de 16 bits sem perdas |
| Sol | Disco observado HMI 4096², referência de 21/09/2026 | JPEG qualidade 94 |

Cada camada inclui uma pirâmide de resolução para ler apenas o nível
e os blocos necessários diretamente do pacote local. As bordas têm dois pixels de sobreposição; mapas globais
repetem a longitude na emenda e preservam o norte no topo. Os arquivos ficam
organizados por camada e nível. O manifesto declara dimensões, projeções, limites,
encoding físico do relevo, bytes e SHA-256 de cada bloco. `SOURCES.json` registra
os arquivos originais, suas datas, URLs, créditos e hashes. O tamanho real do pacote
é informado ao terminar; o espaço reservado inclui também os originais.
Os blocos avulsos ficam somente na pasta de preparação, nunca na distribuição.

São referências estáticas. O mosaico lunar conserva iluminação da aquisição e
é monocromático; seu relevo vem de uma fonte separada. A imagem solar representa
um hemisfério observado e não inventa manchas no verso. Nuvens e efemérides
continuam sendo consultadas separadamente e não entram nessa biblioteca.

Fontes oficiais: [NASA Blue Marble](https://science.nasa.gov/earth/earth-observatory/blue-marble-next-generation/base-map/),
[NASA Black Marble](https://science.nasa.gov/earth/earth-observatory/earth-at-night/maps/),
[LROC WAC](https://data.lroc.im-ldi.com/lroc/view_rdr/WAC_GLOBAL),
[NASA Moon Kit / LOLA](https://svs.gsfc.nasa.gov/4720/) e
[NASA SDO/HMI](https://sdo.gsfc.nasa.gov/data/channels.php).

## Comandos de manutenção

No Linux, também é possível separar download e processamento:

```sh
bash scripts/prepare-assets.sh --download-only
bash scripts/prepare-assets.sh --process-only
bash scripts/prepare-assets.sh --verify
bash scripts/prepare-assets.sh --pack-only
```

`--jobs 3` limita downloads simultâneos (máximo 4). `--layers earth-night sun`
permite processar somente essas camadas; o manifesto final só é publicado quando
todas as seis estiverem prontas. A pasta padrão é `asset-packs`; `--output` deve
apontar para um caminho dentro da montagem `/work` quando usado via Docker.

## Download por níveis

A versão leve não traz `visuals.pak`. `scripts/split-asset-pack.py` divide a
biblioteca reunida em containers `albert-pack-v1` comuns, sem recodificar nada:

- `essential.pak` (~78 MB): todos os blocos com que o programa abre (o nível
  global de cada camada pedido na inicialização, malhas medidas e registros de
  fontes). Todo o Sistema Solar fica navegável com ele.
- `<corpo>-<nível>.pak`: as resoluções seguintes de Terra, Lua, Sol, Mercúrio e
  Marte; `moons-<nível>.pak` reúne as luas. Cada um fica abaixo de 2 GiB.
- `catalog.json`: tamanho, SHA-256 e prioridade de cada container; o id do
  catálogo nomeia a release `assets-<id>` de onde são baixados.

A ordem segue o tamanho acumulado de cada corpo: Sol, Terra e Lua primeiro, nível
a nível, depois luas, Marte e Mercúrio intercalados. O corpo em foco passa à
frente, inclusive interrompendo a transferência em andamento (ela retoma depois
do ponto em que parou). Cada container é baixado para `.part` com HTTP Range,
conferido por tamanho e SHA-256, renomeado atomicamente e só então incorporado;
falhas repetem com espera crescente e o canto inferior esquerdo mostra o andamento
e o botão de nova tentativa. Antes de cada container é reservado espaço em disco.

O leitor junta os containers num único índice e usa, para cada camada, o maior
nível com todos os blocos presentes: sem um nível, aparece a melhor resolução já
baixada, nunca uma imagem inventada. A chegada de um nível mais fino do corpo em
foco recarrega o detalhe regional uma vez. Os arquivos vão para `assets/packs`
ao lado do executável, ou para `data/packs` quando `assets` não é gravável.
A versão completa e o build de desenvolvimento continuam lendo `visuals.pak`.

```sh
python3 scripts/split-asset-pack.py build/linux/imagery/visuals.pak saida --catalog-only
python3 scripts/split-asset-pack.py build/linux/imagery/visuals.pak saida
python3 scripts/light-dist.py dist/linux dist/linux-light
```

Para testar localmente, sirva os containers em `<base>/<release>/<arquivo>` e
defina `ALBERT_ASSET_BASE_URL=<base>` e `ALBERT_ASSET_DIR` com uma pasta de assets
contendo `catalog.json` e sem `visuals.pak`.

## Pacotes por corpo

O build também aceita arquivos adicionais em `assets/imagery/<corpo>/`, cada um
com seu manifesto e partes LFS. O manifesto principal registra essas extensões.
Os três blocos originais permanecem intactos; a montagem verifica cada arquivo
e acrescenta um índice de coleção ao final de **um único `visuals.pak`**.
Não é necessário extrair imagens nem carregar todos os planetas na GPU.

Mercúrio está incluído: superfície MESSENGER 92160 × 46080 e DEM 23040 × 11520,
cinco níveis por camada, 3.411 entradas e 1.693.650.662 bytes no pacote adicional.
O executável só cadastra o corpo quando suas camadas estão efetivamente presentes.
Não substitui assets ausentes por imagens de outro planeta.

Para reproduzir os assets de um corpo declarado em
[planetary-sources.json](../assets/planetary-sources.json), usando Docker:

```sh
# Linux
make assets-body BODY=mercury
```

```powershell
# Windows, Docker usando containers Linux
.\make.cmd assets-body -Body mercury
```

O comando executa `scripts/prepare-body-assets.py` e reutiliza a geração de
pirâmides do pipeline original. Os originais e os blocos intermediários ficam em
`asset-packs/<corpo>/`; somente as partes finais e os manifestos são versionados.
`fetch-ranges.py` baixa intervalos limitados em paralelo, mantém um diário de
SHA-256 por bloco, retoma transferências parciais, respeita `Retry-After` e
recusa misturar versões com ETag/Last-Modified diferentes. O checksum completo
da fonte também é conferido quando declarado no plano.

Apenas para retomar o processamento de uma camada já baixada:

```sh
bash scripts/prepare-assets.sh --body mercury --process-only --layer mercury-height
```

O pacote só é publicado quando todas as camadas declaradas estão prontas.
A montagem normal continua automática no próximo build; esses comandos não
criam versões de distribuição. Camadas novas descrevem seu tipo, profundidade,
escala/offset de altura e no-data no índice, permitindo relevo de outros corpos
sem reutilizar a codificação particular da Lua.
