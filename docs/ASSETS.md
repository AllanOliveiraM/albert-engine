# Biblioteca visual incluída

A biblioteca completa está versionada em **`assets/imagery/`** usando Git LFS.
São três partes de até 1 GiB, reunidas automaticamente em `visuals.pak` durante
o build, com verificação SHA-256 de cada parte e do resultado completo.
O arquivo final tem **2.855.439.799 bytes (2,86 GB / 2,66 GiB)**.
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
