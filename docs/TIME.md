# Referência de tempo

O Albert consulta **Cloudflare, NIST e PTB** por NTP, sem modificar o relógio do
sistema operacional. Astronomia, cena, nuvens, interface e gRPC compartilham
`albert::time::clock()`. O relógio civil só inicializa uma estimativa provisória
quando ainda não existe confirmação externa.

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
continuam no ERFA; DUT1 e movimento polar ainda usam as aproximações já
documentadas pelo projeto. Timestamp recebido de um integrador permanece
inalterado. Data local muito incorreta ainda pode impedir HTTPS por validade
dos certificados do sistema; corrigir o relógio interno não altera essa política.

## API e validação

`TimeService.GetTime` retorna a referência do último quadro, também incluída em
`Scene.clock`. São expostos UTC, qualidade, fonte, última sincronização,
incerteza, RTT, correção pendente, erro e geração. O exemplo C++ usa essa
referência para seu voo sintético; `albert_demo_client 127.0.0.1:50051 --time`
apenas a consulta. Consulte [RPC.md](RPC.md).

`external_time` usa fixtures sem internet: relógios civis dez anos errados,
respostas inválidas/antigas, latência, rollover, duplicação de fontes, outlier,
quorum ambíguo, perda de rede, avanço após suspensão, expiração, correção gradual
e reconexão. `rpc_commands` confere transporte real de qualidade/proveniência e
consistência entre relógio e cena. `time_test --network` é uma verificação manual
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
