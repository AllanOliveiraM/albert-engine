# Albert Engine

Explore a Terra, a Lua e o Sol em 3D, com posições astronômicas reais,
imagens de satélite e nuvens ao vivo opcionais.

## Abrir o programa

Se você recebeu a versão pronta, extraia **a pasta inteira** e abra
**AlbertEngine** (Linux) ou **AlbertEngine.exe** (Windows).
Não é preciso instalar o aplicativo. Mantenha as pastas que acompanham o executável.

É necessário acesso à internet e uma placa de vídeo com OpenGL 3.3 ou superior.
Use a versão correspondente ao seu sistema.

## Compilar a partir deste projeto

Clone este repositório com **Git** e abra um terminal na pasta dele.

```sh
git clone https://github.com/AllanOliveiraM/albert-engine.git
cd albert-engine
```

A preparação abaixo instala as ferramentas necessárias e obtém as imagens
versionadas com **Git LFS**; pode pedir sua senha de administrador.
A biblioteca visual tem **2,86 GB**. Ela já vem processada: o build apenas
reúne suas três partes em um arquivo, verificando a integridade.
A primeira compilação também baixa as bibliotecas de código.
Ela pode levar vários minutos; as próximas reaproveitam o que já foi compilado.
`make run` prepara apenas o aplicativo. Use `make test` para compilar e executar os testes.

**Linux** — Ubuntu 24.04+, Debian 12+ ou Fedora:

```sh
bash scripts/setup-linux.sh
make run
```

**Windows 10/11** — abra o PowerShell na pasta do projeto:

Tenha o Git para Windows instalado com o componente **Git LFS**.

```powershell
.\make.cmd setup
.\make.cmd run
```

O build reúne automaticamente os arquivos de `assets/imagery/` em um único
`visuals.pak`, sem perder qualidade. Não é necessário juntar partes manualmente
nem usar Docker para a compilação normal.

### Se aparecer “Git LFS images missing”

O clone contém apenas referências às imagens. Na pasta do projeto, execute:

```sh
git lfs install --local
git lfs pull
```

Depois execute `make run` no Linux ou `.\make.cmd run` no Windows.
Se `git lfs` não for reconhecido, rode novamente o setup no Linux; no Windows,
instale o componente Git LFS do Git para Windows. Aguarde o download completo
das três partes (2,86 GB no total).

Quem baixou os fontes pelo botão **Download ZIP** do GitHub pode receber apenas
os ponteiros LFS. Nesse caso, use `git clone` e os comandos acima.
A versão pronta em `dist` já contém todas as imagens e não precisa de Git LFS.

## Criar uma versão para compartilhar

No Linux, execute `make dist`. No Windows, execute `.\make.cmd dist`.
Depois, zipe **a pasta inteira** `dist/linux` ou `dist/windows`.
O próprio executável fica na raiz; fontes, logs e cache não entram no pacote.
README, documentação e informações de build também ficam fora da distribuição.
Os avisos obrigatórios de terceiros ficam em `licenses`.

Para gerar as duas versões pelo Linux, com Docker já instalado:

```sh
bash scripts/build-portable.sh all
```

Esse comando usa Debian 12 como base Linux para ampliar a compatibilidade.
Cada plataforma recebe seu próprio executável; o Linux ainda depende do driver
gráfico e de componentes básicos do sistema.

## Navegar

- Escolha **Terra**, **Lua**, **Sol** ou **Tudo** na barra superior.
- Arraste para girar e use a roda do mouse para aproximar.
- Use **Restaurar zoom**, no canto inferior direito, para voltar à distância inicial sem mudar o ângulo da vista.
- Abra **☰ → Cena** para ajustar a imagem e ver as órbitas de **Terra e Lua** ou do **Sistema solar**.
- As **Nuvens em tempo real** começam desligadas. Ative em **Cena** para buscar as observações automaticamente.
- Na Terra e na Lua, ative a opção de **clarear o lado escuro** e ajuste o brilho se desejar. Essa ajuda visual começa desligada.
- Em **Dados**, acompanhe a atualização. Se uma consulta falhar, use **Tentar novamente**.
- Em **Dados → Cache e detalhes visuais**, veja o espaço usado pelas nuvens, ajuste o limite (padrão: **2 GB**) ou limpe os downloads.
- Em **Dados → Manutenção dos arquivos**, você pode recarregar as texturas ou limpar e baixar as nuvens novamente.

As nuvens são mosaicos de observações com atraso, não uma fotografia global
instantânea. A data aparece em **Dados**. Superfícies, cidades e a imagem de
referência do Sol são estáticas. Toda a biblioteca de imagens acompanha o programa; os detalhes
do zoom vêm no arquivo `assets/visuals.pak` e são carregados do disco conforme
a aproximação, sem downloads de superfície.

[Mais detalhes sobre compilação, compatibilidade e precisão](docs/DEVELOPMENT.md) ·
[Fontes das imagens e dos dados](assets/README.md) ·
[Preparação da biblioteca visual](docs/ASSETS.md)

[Controlar o Albert por gRPC](docs/RPC.md) · [Plano de evolução](ROADMAP.md)
