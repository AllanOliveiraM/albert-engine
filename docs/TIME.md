# Referência de tempo

O Albert consulta **Cloudflare, NIST e PTB** por NTP, sem modificar o relógio do
sistema operacional. Cena, nuvens, interface e gRPC compartilham
`albert::time::clock()`; a astronomia segue a [data simulada](#data-simulada),
igual a esse relógio enquanto ao vivo. O relógio civil só inicializa uma
estimativa provisória quando ainda não existe confirmação externa.

## Funcionamento

Um worker consulta três operadores independentes em UDP/123. São necessárias
pelo menos duas respostas com intervalos de incerteza compatíveis. Um terceiro
servidor discrepante é descartado; duas concordâncias distintas e ambíguas não
são aceitas. Nome ou endereço repetido não contam como fontes independentes.

Cada resposta é conferida: origem/nonce, versão/modo, stratum, indicadores de
sincronização, timestamps de referência/recepção/transmissão, tempo de
processamento, atraso e distância da referência. O cálculo usa o tempo de ida
e volta medido por relógio monotônico e desconta o processamento no servidor;
não precisa que o relógio civil da máquina esteja correto. A era NTP é resolvida
na janela explícita **2020–2100**, inclusive no rollover de 2036.

Entre consultas, UTC avança pelo relógio monotônico: `CLOCK_BOOTTIME` no Linux,
QPC no Windows, ambos incluindo suspensão. Correções de até um segundo são
aplicadas gradualmente, limitadas a 10 ms/s, para manter a continuidade. A
primeira sincronização e correções maiores mudam a referência imediatamente e
invalidam a geração de efemérides; o render solicita dados novos. Uma pausa
superior a 30 segundos também provoca nova consulta e atualização das posições.

A consulta normal ocorre a cada cinco minutos, com pequeno jitter. Falhas usam
esperas de 1, 2, 4, 8, 16 e até 30 minutos. O botão **Conferir horário**, em
**Dados**, antecipa a próxima tentativa respeitando o intervalo mínimo de um
minuto por servidor. KoD aplica uma espera de uma hora; `DENY`/`RSTR` desativam
a fonte até reiniciar. DNS usa o resolver assíncrono do libcurl com limite de
dois segundos; a consulta UDP tem limite de um segundo e cancelamento. O
carregamento inicial espera a primeira rodada por até 12 segundos e então pode
prosseguir com a estimativa local, identificada na interface.

## Qualidade e limites

| Estado | Significado |
| --- | --- |
| `LOCAL` | Ainda não houve concordância externa; incerteza desconhecida (`-1`) |
| `SYNCHRONIZED` | Concordância externa recente, sem falha posterior |
| `HOLDOVER` | A última referência externa continua avançando; falha de consulta ou idade superior a 10 minutos |
| `EXPIRED` | Mais de uma hora sem confirmação; horário apenas estimado |

O painel **Dados → Detalhes técnicos** informa fontes, atraso, idade da
sincronização, erro e incerteza estimada. A estimativa considera atraso,
dispersão/referência do servidor, correção pendente e crescimento conservador de
50 ppm entre consultas. **Não é uma garantia de precisão nem um limite físico
certificado**; assimetria de rede e deriva do hardware afetam o resultado.
A última referência é mantida durante a sessão, inclusive offline; reiniciar
sem rede volta à estimativa local. Nenhum arquivo persistido é tratado como
relógio confiável após desligar a máquina.

**Este corte usa SNTP/NTP sem autenticação criptográfica.** Concordância de
operadores aumenta a resistência a uma fonte incorreta, mas não protege de
um atacante que controle a rede. `authenticated` permanece `false`. NTS foi
avaliado e continua como extensão: requer negociação TLS, cookies e AEAD
conforme RFC 8915, implementação compatível com os backends Linux/Windows e
uma política de bootstrap dos certificados quando a data local está errada.
Não há autenticação NTS simulada nem validação TLS desativada.

As fontes escolhidas distribuem UTC sem misturar o smear do Google. Anúncio de
leap second (`LI=1/2`) é conservadoramente rejeitado nesta primeira versão,
marcando degradação se não houver quorum; `LI=3` também é rejeitado. A passagem
pelo segundo intercalar com precisão integral ainda é uma pendência explícita,
não uma escala suavizada apresentada como UTC. As conversões UTC/TAI/TT/TDB
usam ERFA em `core/astro_time`, incluindo `eraDtdb` geocêntrico e conversão
inversa para timestamps UTC recebidos do Horizons. DUT1 e movimento polar ainda usam as aproximações já
documentadas pelo projeto. Timestamp recebido de um integrador permanece
inalterado. Data local muito incorreta ainda pode impedir HTTPS por validade
dos certificados do sistema; corrigir o relógio interno não altera essa política.

## Data simulada

O instante astronômico da cena é o relógio acima enquanto **ao vivo**. Ao pausar,
acelerar, voltar no tempo ou escolher uma data, ele passa a ser um instante
escolhido que avança `rate` segundos simulados por segundo do relógio monotônico
(`core/simulation`). Correções NTP não movem uma data escolhida; uma pausa superior a
30 segundos do programa (suspensão) a mantém onde estava. Intervalo: 1900-01-01
00:00:00 a 2100-12-31 23:59:59 UTC; `|rate|` até uma semana por segundo. Nos
limites, a simulação para e informa `limited`. A geração muda ao escolher uma data
e ao voltar para **Agora**, não ao mudar a velocidade. Datas civis usam o calendário
gregoriano proléptico implementado no projeto, porque a biblioteca C do Windows não
converte instantes anteriores a 1970.

Antes de 1972 o UTC tinha segundos de duração variável, que o ERFA modela desde
1960. Antes de 1960 o instante escolhido é tratado como tempo universal e
TT = UT + 32,184 s; o erro em TT chega a cerca de 35 s em 1900 (dezenas de
quilômetros na posição da Lua).

**Eventos.** O painel lista momentos para visitar, pausados, com a câmera chegando pelo
lado que os mostra: eclipses solares totais de 8/4/2024 e 2/8/2027 (vista do lado do
Sol, com a sombra da Lua sobre a Terra), eclipse lunar total de 3/3/2026 (Lua vista da
Terra), pouso da Apollo 11 (20/7/1969 20:17:40 UTC), passagem da Giotto pelo Halley
(14/3/1986 00:03:02 UTC) e periélio da 67P (13/8/2015 02:03 UTC); nos cometas o realce
liga e a câmera enquadra coma e cauda. Os eclipses usam o máximo em TD das cartas de
F. Espenak (NASA GSFC) menos o ΔT observado (~69 s). No eclipse de 2027 a sombra cai
sobre o Egito, perto de Luxor, como no ponto de máximo publicado (25°29′N, 33°13′E).

**Efemérides por data.** Cada tabela do Horizons cobre ao menos um dia simulado e
os próximos 600 s de reprodução, mais o tempo medido da última consulta, com no
máximo 200 linhas. O passo é escolhido pela precisão do Hermite cúbico, cujo erro
cresce com (2π·passo/período)⁴/384: Lua de 1 h a 1 d (até ~3 km), planetas de 1 a
5 d (Vênus até ~100 km, os demais ~20 km). Uma data sem tabela pede primeiro uma
tabela curta (60 s de reprodução) e depois a completa; novas datas abandonam
pedidos em andamento entre consultas, e os trajetos cedem a fila do JPL às
posições. Cada consulta leva de um a vários segundos, então as tabelas só
acompanham a reprodução até **6 horas por segundo**.

**Posições aproximadas.** Fora das tabelas, Sol, Lua e planetas usam modelos
analíticos do ERFA: `epv00` para a Terra (≈5 km), `moon98` para a Lua (6 km RMS,
32 km no pior caso em 1950–2100) e `plan94` para os planetas (direção dentro de
1,5′; até ~2 000 km em Mercúrio, 3 700 km em Vênus, 30 000 km em Marte, 300 000 km
em Júpiter, 650 000 km em Saturno, 1,3 milhão de km em Urano e 350 000 km em
Netuno). Cada corpo soma a diferença entre tabela e modelo medida no último
instante coberto; após uma nova data não há diferença conhecida. Quando uma
tabela chega, a diferença entra em 0,6 s. O painel e a API (`approximate_positions`)
indicam esse estado; acima de 6 h/s ele é permanente até pausar ou desacelerar.

As tabelas ficam em cache (memória do provedor com 96 tabelas e disco), então
voltar a uma data já visitada não repete consultas. As estrelas recebem o movimento
próprio do catálogo para a data, no shader.

Os trajetos orbitais ficam centrados no instante e são buscados de novo após 60
dias simulados, no máximo a cada 30 s e só até 6 h/s. Fora das amostras, o destaque
de passado e futuro é dobrado por revoluções inteiras (até três); além disso fica
oculto até o novo trajeto chegar. Outras luas e pequenos corpos usam órbitas de
dois corpos com elementos de outubro de 2026, menos precisas longe dessa época.
Objetos do catálogo seguem a data e a velocidade com tabelas de até 240 linhas;
em velocidades altas podem ficar marcados como desatualizados. As nuvens
observadas só aparecem a menos de um dia do presente.

## API e validação

`TimeService.GetTime` retorna a referência do último quadro, também incluída em
`Scene.clock`. São expostos UTC, qualidade, fonte, última sincronização,
incerteza, RTT, correção pendente, erro e geração. `simulation` traz o instante
simulado, velocidade, modo ao vivo, limite, geração e se as posições são
aproximadas; `TimeService.SetTime` escolhe data e velocidade ou volta ao vivo. O exemplo C++ usa essa
referência para seu voo sintético; `albert_demo_client 127.0.0.1:50051 --time`
apenas a consulta. Consulte [RPC.md](RPC.md).

`external_time` usa fixtures sem internet: relógios civis dez anos errados,
respostas inválidas/antigas, latência, rollover, duplicação de fontes, outlier,
quorum ambíguo, perda de rede, avanço após suspensão, expiração, correção gradual
e reconexão. `simulated_time` cobre o relógio simulado (continuidade, saltos,
limites, retenção, validação atômica), datas civis anteriores a 1970 e regras de
anos bissextos, e os modelos do ERFA contra o Horizons em 1969 e 2026.
`astronomical_providers` confere as janelas por velocidade, `catalog_import` a
troca de data e a retenção sem novas consultas, e `rpc_commands` o transporte real
de qualidade/proveniência, `SetTime` e a consistência entre relógio e cena. `time_test --network` é uma verificação manual
com os servidores públicos, fora do CTest para manter a suíte independente da
rede. A suspensão é exercitada com avanço de tempo nas fixtures; não é um teste
de suspensão física do computador.

## Referências

- [NTP/SNTP — RFC 5905](https://www.rfc-editor.org/rfc/rfc5905).
- [NTS — RFC 8915](https://www.rfc-editor.org/rfc/rfc8915).
- [Cloudflare NTP e política sem smear](https://developers.cloudflare.com/time-services/ntp/).
- [Servidores e limites do NIST](https://tf.nist.gov/tf-cgi/servers.cgi/en-en/).
- [Leap seconds no NIST ITS](https://www.nist.gov/pml/time-and-frequency-division/time-distribution/internet-time-service-its).
- [Serviço de tempo da PTB](https://www.ptb.de/cms/en/ptb/fachabteilungen/abt4/fb-44/fragenzurzeit/fragenzurzeit07.html).
- [QPC e suspensão no Windows](https://learn.microsoft.com/en-us/windows/win32/sysinfo/acquiring-high-resolution-time-stamps).
