<p align="center">
  <img src="assets/branding/albert.png" width="80" alt="Ícone do Albert Engine">
</p>

<h1 align="center">Albert Engine</h1>

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

Hoje, o conteúdo embarcado inclui **Terra, Lua, Sol e Mercúrio**. O catálogo interno permite
importar outros planetas com posições reais e visual simplificado. Objetos
próprios podem ser criados pela API como esferas, caixas e cilindros. Novos planetas, luas,
anéis, modelos de veículos, camadas de mapas e provedores de dados estão no
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
| Catálogo interno | Importação de planetas pela interface ou gRPC, com posições reais e acompanhamento automático |
| Controle externo | API gRPC local para consultar a cena, criar/remover objetos e transmitir posição, velocidade e orientação |
| Distribuição | Executável portátil para Linux e Windows, com a biblioteca visual acompanhando o programa |

## Como funciona

**O tempo e os dados definem a cena.** O Albert consulta as posições do Sol e
da Lua e as trajetórias da Terra e Lua no Horizons. Entre consultas, interpola
as amostras para manter o movimento contínuo. A orientação dos corpos usa
transformações astronômicas; a Terra considera o modelo WGS84.

O horário é conferido automaticamente com **fontes NTP independentes** e
continua avançando entre consultas. Se a rede falhar, a interface informa quando
o horário é estimado ou local. Fonte e qualidade ficam em **Dados** e também
estão disponíveis pela API. Esta versão ainda não autentica NTP com NTS;
[funcionamento e limites](docs/TIME.md).

**As imagens estáticas acompanham o aplicativo.** Superfícies, luzes das cidades,
relevo e referência solar estão em uma biblioteca de aproximadamente **4,55 GB**. Na distribuição,
ela fica em um único `assets/visuals.pak`. Aproximar a câmera carrega recortes
mais detalhados dessa biblioteca; a resolução máxima depende dos dados de origem.

**Os recursos entram conforme a necessidade.** O Albert prepara os detalhes em
segundo plano e divide o envio de texturas à GPU entre quadros, mantendo a
superfície anterior enquanto a nova fica pronta. A origem gráfica acompanha o
alvo para preservar detalhes pequenos em distâncias astronômicas.

**As nuvens são observações com atraso.** Elas começam desligadas. Quando
ativadas, o Albert consulta mosaicos de satélite, mostra a data em **Dados** e
mantém um cache de downloads. O limite padrão é 2 GB e pode ser ajustado no menu.
A aparência solar e os mosaicos de superfície são referências estáticas.

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

- Escolha **Terra**, **Lua**, **Sol** ou **Tudo** na barra superior.
- Arraste para girar e use a roda do mouse para aproximar.
- Use o botão **Restaurar zoom**, no canto inferior direito, para voltar à distância inicial.
- Abra **☰ → Cena** para ajustar atmosfera, nuvens, cidades, fronteiras, sombras e órbitas.
- Ative **Nuvens em tempo real** em **Cena** para buscar as observações automaticamente.
- O brilho auxiliar da Terra e Lua ajuda a enxergar o lado escuro; começa desligado e é ajustável.
- Em **Dados**, acompanhe as atualizações, retente consultas e gerencie o cache.
- Em **Dados → Adicionar objetos**, importe planetas com posições reais; os que ainda não têm imagens embarcadas aparecem como esferas simplificadas.
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
e clientes para assistentes de IA. O primeiro catálogo interno já reutiliza o Horizons. Modelos externos, camadas
geográficas e novas fontes públicas fazem parte das próximas entregas.

## Criar uma versão para compartilhar

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
