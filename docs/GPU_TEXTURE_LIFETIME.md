# Liberação de texturas GPU durante zoom e troca de regiões

Revisão de 3/11/2026. Esta é a primeira prioridade da [fila de ajustes](visual-improvements-queue.md), porque o usuário relatou encerramento do Albert ao aproximar a Terra e abrir o menu.

## Defeito reproduzido

A correção local anterior já observava o evento `Texture::dispose()` e retirava do cache a cópia GPU atual e o upload pendente. Entretanto, invalidava os grupos de material apenas pelo serial da **última** view da textura.

Quando dois materiais compartilhavam a mesma imagem, um deles podia ficar oculto enquanto o outro carregava uma nova versão. O material oculto mantinha seu grupo antigo, que ainda referenciava a versão anterior na GPU. Descartar a textura mais recente não retirava essa referência antiga. A contagem de texturas do cache podia cair para zero enquanto o grupo de material ainda mantinha memória viva.

O teste reproduziu exatamente esse estado antes da correção:

```text
disposed tile cycle 0: 0 textures, 0 bytes, 1 material groups
Disposed tile retained an older view in a hidden material
```

Isso identifica uma brecha na liberação; não estabelece que era a única causa do esgotamento de VRAM relatado.

## Correção

- Os grupos de material agora registram a identidade das texturas de origem, além dos seriais das views usados para atualização do desenho.
- O descarte invalida todos os grupos daquela textura, inclusive versões antigas em materiais ocultos. O descarte por expiração do cache passa pela mesma rotina.
- O upload pendente e a cópia corrente são liberados. Se a imagem voltar a ser mostrada, os recursos e vínculos são reconstruídos a partir dos dados CPU ainda disponíveis.
- O diagnóstico de memória soma os bytes das cópias mostrada e pendente separadamente, incluindo seus mipmaps, em vez de aplicar as dimensões/formato da cópia nova a ambas.

O caminho libera referências de WebGPU; não usa `wgpuTextureDestroy()` para invalidar recursos que ainda podem estar referenciados por comandos gravados. A distinção entre liberação de referências, destruição explícita e execução de comandos é relevante no [modelo de recursos do WebGPU](https://www.w3.org/TR/webgpu/#resource-usages).

`SceneRenderer::textureCacheStats()` informa entradas e bytes diretamente possuídos pelo cache e a contagem de grupos de material. Não mede VRAM total: alvos de renderização, atmosferas, volumes, recursos retidos por comandos e alocações internas do driver ficam fora dessa contagem.

## Evidência

O teste `gpu_scene_test` usa dois materiais e uma imagem RGBA de 4096×4096 (64 MiB, sem mipmaps), oculta um material, completa uma atualização em oito frames, descarta a textura e confere que nenhum grupo permanece. Em seguida, recria a imagem, inicia outra atualização, verifica os 128 MiB das duas cópias e cancela o upload. Repete o ciclo três vezes e verifica a destruição da textura CPU. Também confere pixels e ausência de erros de validação, não apenas contadores.

| Adaptador selecionado e efetivamente reportado | Resultado do teste |
| --- | --- |
| llvmpipe (LLVM 21.1.8, 256 bits) | Aprovado; 0 entradas/bytes/grupos após cada descarte |
| NVIDIA GeForce MX350 | Aprovado; 0 entradas/bytes/grupos após cada descarte |
| Intel Iris Xe Graphics (TGL GT2) | Aprovado; 0 entradas/bytes/grupos após cada descarte |

Logs em `build/mission-review/gpu-texture-lifetime/`: `before.log`, `software.log`, `hardware-high.log`, `hardware-low.log`, `build-after.log` e os testes de integração. A build de `AlbertEngine` e `gpu_scene_test` passou.

Os quatro testes de integração `visual_webgpu`, `webgpu_ocean`, `webgpu_clouds` e `webgpu_atmosphere` também passaram em software (54,53 s), após recompilar seus alvos. Nenhuma definição da missão foi alterada.

Os mesmos testes também cobrem a troca de um índice com versão zero, a rejeição do índice inválido `26112` numa malha com 576 vértices e a recuperação do desenho após corrigir os índices. As mensagens de rejeição desses dados de teste são intencionais e não são erros de validação submetidos à GPU.

Reprodução:

```sh
cmake --build build/linux --target AlbertEngine gpu_scene_test -j2
ALBERT_GPU=software ./build/linux/tests/gpu_scene_test
ALBERT_GPU=high ./build/linux/tests/gpu_scene_test
ALBERT_GPU=low ./build/linux/tests/gpu_scene_test
```

## Validação integrada em 3/11

Acrescentado o modo explícito `AlbertEngine --review-earth`: 180 segundos depois do carregamento inicial, duas sequências de vista orbital, aproximação, deslocamento entre regiões e afastamento, alternando abertura do menu. Encerra automaticamente e exige pelo menos quatro imagens regionais carregadas e nenhum erro WebGPU. Não é ativado na execução normal. Interrupção antes do término não conta como aprovação.

As duas instâncias usaram diretórios próprios de preferências, 1280×720, qualidade alta, MSAA 4x, relevo, nuvens volumétricas e ondas habilitados, sem áudio. Não houve missão ou vista auxiliar de voo neste teste. Cada adaptador completou **oito** cargas regionais, sem crash ou erro de validação. O cache retornou à mesma base de **11 texturas / 149,4 MiB** nos afastamentos dos dois ciclos. Durante as trocas, o registro chegou a 313,4 MiB de texturas.

| Adaptador reportado | Resultado | Medição externa |
| --- | --- | --- |
| NVIDIA GeForce MX350 | Aprovado; 242,5 s incluindo carga inicial | Pico de 791 MiB no dispositivo inteiro, amostrado a cada segundo com nvidia-smi; não é atribuição exclusiva ao Albert. |
| Intel Iris Xe (TGL GT2) | Aprovado; 224,2 s incluindo carga inicial | Pico de RSS do processo de 1.421,1 MiB; RAM compartilhada, **não** uma medição de VRAM. |

Artefatos: `build/mission-review/gpu-texture-lifetime/app-high/` e `app-low/`, com `app.log`, `memory.csv`, `result.json` e as preferências isoladas em `data/settings.json`. As bibliotecas de imagens vieram de `build/linux/imagery/visuals-v2.pak`; o cache de dados dinâmicos foi compartilhado por symlinks com `build/simulation-cache`.

Para repetir com essas mesmas preferências isoladas (os diretórios precisam existir):

```sh
ALBERT_GPU=high ALBERT_AUDIO=0 ALBERT_WINDOW=1280x720 ALBERT_DATA_DIR="$PWD/build/mission-review/gpu-texture-lifetime/app-high/data" ./build/linux/bin/AlbertEngine --review-earth
ALBERT_GPU=low ALBERT_AUDIO=0 ALBERT_WINDOW=1280x720 ALBERT_DATA_DIR="$PWD/build/mission-review/gpu-texture-lifetime/app-low/data" ./build/linux/bin/AlbertEngine --review-earth
```

## Cobertura ainda pendente

Esse resultado cobre a reprodução controlada de zoom/menu em duas GPUs, não todo esgotamento de memória possível. Ainda verificar sessão longa com voo/vista auxiliar, redimensionamentos repetidos, nuvens observadas, trocas de qualidade e outros corpos. O backend ImGui mantém referências a vistas auxiliares aposentadas até o encerramento; avaliar a retenção ao redimensionar essas vistas repetidamente. Se houver pressão restante, dimensionar orçamento residente e de uploads pelas medições, sem ocultar o defeito usando outra GPU.
