<p align="center">
  <img src="assets/branding/albert.png" width="80" alt="Ícone do Albert Engine">
</p>

<h1 align="center">Albert Engine</h1>

[![Build and tests](https://github.com/AllanOliveiraM/albert-engine/actions/workflows/build.yml/badge.svg)](https://github.com/AllanOliveiraM/albert-engine/actions/workflows/build.yml)

<p align="center">Exploração astronômica em 3D, com dados reais e controle programável.</p>

<p align="center">
  <a href="docs/images/earth.webp"><img src="docs/images/earth.webp" width="49%" alt="Terra renderizada no Albert Engine"></a>
  <a href="docs/images/moon.webp"><img src="docs/images/moon.webp" width="49%" alt="Lua renderizada no Albert Engine"></a>
</p>

<p align="center"><em>Capturas reais do Albert. Clique nas imagens para ampliar.</em></p>

## O que é o Albert?

O Albert Engine é um simulador astronômico escrito em C++20. Ele combina
posições calculadas a partir de dados astronômicos, imagens de observação e
uma câmera livre para explorar os objetos em 3D.

O projeto está se tornando uma plataforma para **visualizar e programar cenas
espaciais**. Você pode navegar pela interface ou conectar outro programa à API
gRPC para criar objetos, transmitir seus movimentos e acompanhar um alvo com a
câmera. A renderização, a navegação e as animações ficam dentro do Albert.

Hoje, a cena inclui **43 corpos**: o Sol, oito planetas, 25 luas, cinco planetas
anões, Vesta, Pallas, Halley e 67P. A seleção é local, organizada por categorias,
e aceita nomes aproximados. Luas adicionais, planetas anões e pequenos corpos
usam órbitas aproximadas embarcadas. Vinte e uma luas adicionais já têm mapas
próprios NASA, detalhes por zoom e orientação IAU; outras superfícies adicionais
continuam ilustrativas. Fobos e Deimos usam modelos de forma medidos de missões,
com mapas Viking; o alinhamento das imagens sobre esses modelos ainda exige validação.
Mimas usa a forma medida de Gaskell e o mosaico Cassini em cores realçadas,
reprojetado e alinhado à forma, com conferência por crateras catalogadas pela IAU.
Febe usa a forma medida de Gaskell e o mapa Cassini de 2005; as áreas nunca
observadas aparecem em cinza neutro, identificadas no painel. Hipérion usa a forma
medida de Thomas com a cor média medida pela Cassini, sem mapa de albedo; como sua
rotação é caótica, a orientação é ilustrativa. Proteu usa a forma de Stooke derivada
das imagens da Voyager 2, com cor neutra uniforme e sem mapa de albedo. Nereida, que
nunca foi resolvida em imagem, aparece como esfera com o albedo medido e um aviso.
Vesta e Ceres usam o mosaico e o relevo medidos pela sonda Dawn, e Plutão e Caronte
os da New Horizons, com detalhe por zoom; o sul de Plutão, em noite polar no sobrevoo, aparece
em cinza neutro.
O cometa 67P usa a forma medida pela câmera OSIRIS da Rosetta, com a cor escura do
núcleo medida pela missão; Halley usa a forma estimada com as imagens da Giotto e
das Vega, muito incerta, com rotação ilustrativa.
Júpiter, Urano e Netuno têm os anéis das tabelas do PDS, tênues como na realidade, com
uma opção de realce no painel do planeta.
Titã aparece como em luz visível, coberta pela névoa alaranjada; a superfície em
infravermelho (938 nm) da Cassini é uma opção identificada no painel.
Os oito planetas e a Lua preservam suas efemérides Horizons
e mapas próprios.
Os cinturões de asteroides e Kuiper e a nuvem de Oort são populações visuais
estatísticas, exibidas conforme a escala, sem representar trajetórias individuais.
Objetos próprios podem ser criados pela API como esferas, caixas, cilindros ou
modelos GLB estáticos. Consultas/importações online ficam disponíveis pela API;
a interface não busca objetos na internet.
Novos planetas, luas, anéis, camadas de mapas e provedores de dados estão no
[roadmap](ROADMAP.md).

## O que já funciona

| Recurso | O que você encontra no programa |
| --- | --- |
| Posições astronômicas | Consultas ao NASA/JPL Horizons, atualização automática e movimento interpolado entre amostras |
| Escala física | Dimensões e distâncias reais, Terra WGS84 e orientação dos corpos |
| Visualização | Superfícies detalhadas, relevo regional, iluminação solar, atmosfera terrestre e luzes das cidades |
| Zoom | Detalhes carregados do disco conforme a região visível e a aproximação |
| Órbitas | Trajetórias dos corpos embarcados, com cores e fades para distinguir passado e futuro |
| Céu | Estrelas baseadas em catálogo astronômico, com posições e brilhos relativos |
| Nuvens | Observações de satélite opcionais, atualizadas automaticamente quando ativadas |
| Navegação | Foco por objeto, órbita manual, zoom e transições animadas |
| Apresentação | Órbita lenta ao chegar a um objeto; arraste ou use o zoom para parar |
| Pequenos corpos | Formas rochosas individuais; representações artísticas identificadas, sem novas imagens baixadas |
| Eclipses Terra/Lua | Umbra, penumbra e eclipses anulares; luz avermelhada lunar aproximada. [Limites](docs/ECLIPSES.md) |
| Camadas | Pontos e linhas programáveis, inclusive sobre o relevo terrestre, com controle no menu |
| Catálogo local | Categorias, busca aproximada e foco em corpos já incluídos; objetos adicionais entram pela API |
| Controle externo | API gRPC local para consultar a cena, importar GLB estático, criar/remover objetos e transmitir posição, velocidade e orientação |
| Distribuição | Executável portátil para Linux e Windows, com a biblioteca visual acompanhando o programa |

## Como funciona

**O tempo e os dados definem a cena.** O Albert consulta as posições e trajetórias dos corpos embarcados no Horizons. Entre consultas, interpola
as amostras para manter o movimento contínuo. A orientação dos corpos usa
transformações astronômicas; a Terra considera o modelo WGS84.

O horário é conferido automaticamente com **fontes NTP independentes** e
continua avançando entre consultas. Se a rede falhar, a interface informa quando
o horário é estimado ou local. Fonte e qualidade ficam em **Dados** e também
estão disponíveis pela API. Esta versão ainda não autentica NTP com NTS;
[funcionamento e limites](docs/TIME.md).

**As imagens estáticas ficam no computador, completas ou baixadas aos poucos.**
Superfícies, luzes das cidades, relevo e referência solar formam uma biblioteca de
aproximadamente **4,89 GB**. A versão completa a traz em um único `assets/visuals.pak`
e funciona offline. A versão leve baixa ao abrir só o essencial (~78 MB, todos os
corpos em resolução global) e completa as demais resoluções em segundo plano, com
o corpo em foco à frente; o canto inferior esquerdo mostra o andamento. Aproximar
a câmera carrega recortes mais detalhados; a resolução máxima depende dos dados de
origem e, na versão leve, do que já chegou.

**Os recursos entram conforme a necessidade.** O Albert prepara os detalhes em
segundo plano e divide o envio de texturas à GPU entre quadros, mantendo a
superfície anterior enquanto a nova fica pronta. A origem gráfica acompanha o
alvo para preservar detalhes pequenos em distâncias astronômicas.
Ao afastar a câmera ou mudar de objeto, os recortes de alta resolução são
liberados após um breve intervalo, reduzindo o consumo de memória.

**Os downloads recentes são reutilizados.** Posições e órbitas ficam salvas
entre execuções. Se os dados ainda cobrem o horário atual, a cena pode abrir
com eles e renovar as consultas em segundo plano. Tabelas expiradas ou
corrompidas são descartadas; o Albert não extrapola posições para esconder
uma falha de conexão. A primeira abertura ainda precisa buscar os dados.

**As nuvens são observações com atraso.** Elas começam desligadas. Quando
ativadas, o Albert consulta mosaicos de satélite, mostra a data em **Dados** e
mantém um cache de downloads. O limite padrão é 2 GB e pode ser ajustado no menu.
Em **Dados**, você pode consultar o espaço ocupado, limitar ou limpar o cache
de posições, órbitas, buscas e nuvens sem apagar as imagens incluídas.
A aparência solar e os mosaicos de superfície são referências estáticas.
Nos novos planetas, os mapas têm resoluções de 1K a 32K e não incluem
relevo medido. Os gigantes e Vênus usam referências visuais de atmosfera;
as nuvens desses corpos não são atualizadas ao vivo. Consulte
[fontes e limites dos mapas](docs/PLANETARY_ASSETS.md).

**O catálogo offline distingue aproximação de efeméride.** Os 33 corpos
adicionais são propagados a partir de elementos orbitais osculantes de
16/10/2026. A aproximação de dois corpos não integra perturbações e sua precisão
diminui longe dessa época. Os nomes e categorias estão disponíveis sem consultas
online. [Fontes, reprodução e limites](docs/LOCAL_SYSTEM.md).

**A interface e o gRPC compartilham a mesma cena.** Um cliente externo pode
criar um veículo, enviar sua posição e orientação e solicitar acompanhamento.
Você continua podendo orbitar e aproximar a câmera durante esse movimento.
Os formatos e limites disponíveis estão em [RPC.md](docs/RPC.md).

## Abrir o programa

Se você recebeu uma versão pronta, extraia **a pasta inteira** e abra
**AlbertEngine** no Linux ou **AlbertEngine.exe** no Windows. Mantenha as pastas
que acompanham o executável; o aplicativo funciona sem instalação.

São necessários **OpenGL 3.3 ou superior** e acesso à internet para consultar
os dados astronômicos e, quando ativadas, as nuvens. Use a versão do seu sistema.

## Compilar a partir do projeto

Clone com Git e abra um terminal na pasta do projeto:

```sh
git clone https://github.com/AllanOliveiraM/albert-engine.git
cd albert-engine
```

A preparação instala as ferramentas e dependências do sistema e baixa as
imagens versionadas com **Git LFS**. Pode pedir senha de administrador.
A primeira compilação também baixa e compila as bibliotecas de código e pode
levar vários minutos. As próximas reaproveitam o que já foi compilado.

**Linux — Ubuntu 24.04+, Debian 12+ ou Fedora:**

```sh
bash scripts/setup-linux.sh
make run
```

Para o ícone aparecer corretamente na barra e no menu de aplicativos do Linux,
execute `make desktop` depois de compilar e reabra o Albert. Esse comando cria
o atalho no seu usuário, mantendo o programa na pasta original.

**Windows 10/11 — PowerShell:**

Tenha o Git para Windows com o componente **Git LFS** instalado.

```powershell
.\make.cmd setup
.\make.cmd run
```

O build reúne as partes das imagens em `visuals.pak` e verifica sua
integridade. Não é necessário juntar os arquivos manualmente nem usar Docker
na compilação nativa. `make run` prepara apenas o aplicativo; `make test`
compila e executa os testes. No Windows, use `.\make.cmd test`.

### Se aparecer “Git LFS images missing”

Na pasta do projeto, execute:

```sh
git lfs install --local
git lfs pull
```

Aguarde o download dos arquivos, **aproximadamente 4,55 GB no total**, e rode novamente
`make run` ou `.\make.cmd run`. Se `git lfs` não for reconhecido, repita o setup
no Linux ou instale o componente Git LFS do Git para Windows.

O botão **Download ZIP** do GitHub pode fornecer apenas ponteiros para esses
arquivos. Para compilar, prefira `git clone`. A distribuição pronta já contém
as imagens e não precisa de Git LFS.

## Navegar e ajustar a imagem

- Clique no nome do objeto na barra superior: busque planetas e luas ou escolha **Tudo**.
- Para asteroides e cometas, digite um nome/designação e clique em **Buscar asteroides e cometas online**.
- Clique em um resultado com **+** para adicionar e acompanhar suas posições automaticamente.
- Arraste para girar e use a roda do mouse para aproximar.
- Use o botão **Restaurar zoom**, no canto inferior direito, para voltar à distância inicial.
- Abra **☰ → Cena** para ajustar atmosfera, nuvens, cidades, fronteiras, sombras e órbitas.
- Ative **Nuvens em tempo real** em **Cena** para buscar as observações automaticamente.
- O brilho auxiliar de cada objeto com lado escuro ajuda a enxergar o lado escuro; começa desligado e é ajustável.
- Em **Dados**, acompanhe as atualizações, retente consultas e gerencie o cache.
- Em **Dados → Consultas do catálogo**, acompanhe importações e tente novamente após falhas.
- Em **Dados → Manutenção dos arquivos**, recarregue as texturas ou limpe e baixe as nuvens novamente.

## Programar cenas com gRPC

Compile o projeto completo e abra o servidor local:

```sh
make build
./build/linux/bin/AlbertEngine --rpc
```

Em outro terminal, execute o cliente de exemplo:

```sh
./build/linux/src/rpc/albert_demo_client
```

Ele cria um cilindro representando um veículo, foca a câmera e transmite
posição e orientação por cerca de dez segundos. Os dados são **sintéticos**,
identificados como demonstração. A última posição permanece visível ao terminar.

O servidor é ativado com `--rpc` e aceita conexões em `127.0.0.1`.
O [guia gRPC](docs/RPC.md) traz os comandos Windows, contrato, coordenadas,
limites, streaming e remoção do exemplo.

Essa base permite construir integradores de simulação, ferramentas educativas
e clientes para assistentes de IA. O primeiro catálogo interno já reutiliza o Horizons. O suporte inicial a GLB estático também está disponível. Pontos e linhas geográficas também podem ser adicionados por gRPC.
Texturas/animação de modelos, polígonos, rótulos, camadas raster e novas fontes
públicas são próximas entregas.

## Criar uma versão para compartilhar

O GitHub Actions também gera os pacotes dos dois sistemas. Em **Actions → Portable
releases → Run workflow**, você pode testar o empacotamento e baixar os artefatos.
Ao enviar uma tag de versão, como `v0.2.1`, ele compila, testa e publica a release
automaticamente. [Instruções de publicação](docs/DEVELOPMENT.md#releases-no-github-actions).

Como a biblioteca visual ultrapassa o limite por arquivo das releases do GitHub,
o ZIP é dividido em volumes. Baixe todas as partes do seu sistema na mesma pasta
e abra a `.zip.001` com 7-Zip para extrair. O programa fica na raiz da pasta extraída.

Execute `make dist` no Linux ou `.\make.cmd dist` no Windows. Depois, zipe
**a pasta inteira** `dist/linux` ou `dist/windows`.
O executável fica na raiz, acompanhado apenas dos recursos necessários e das
licenças de terceiros. Fontes, logs, cache e documentação ficam fora do pacote.

Com Docker instalado, é possível gerar as duas versões pelo Linux:

```sh
bash scripts/build-portable.sh all
```

O build Linux portátil usa Debian 12 como base. O driver gráfico e os
componentes básicos do sistema continuam sendo fornecidos pela máquina.

## Desenvolvimento e próximos passos

O núcleo mantém estados físicos em dupla precisão, a renderização usa
threepp/OpenGL e Dear ImGui, e as integrações usam libcurl, ERFA, JSON e
gRPC/Protobuf. O CMake declara as versões e os hashes das dependências.

As próximas entregas ampliam os corpos embarcados, modelos externos e camadas,
além de aprofundar a precisão do tempo e os provedores internos de dados. Cada etapa
deve preservar precisão, fluidez e compatibilidade Linux/Windows.

- [Roadmap e andamento](ROADMAP.md)
- [Compilação, arquitetura, precisão e validação](docs/DEVELOPMENT.md)
- [API gRPC e exemplo de integração](docs/RPC.md)
- [Fontes das imagens e dos dados](assets/README.md)
- [Preparação da biblioteca visual](docs/ASSETS.md)
- [Dependências e créditos](THIRD_PARTY.md)
