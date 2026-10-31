# CyberLifePlus — tradução PT-BR

*In English: [`README.md`](README.md).*

Tradução de Inazuma Eleven 3 - Sekai e no Chousen!! The Ogre para o português do Brasil, como mod do
ie3ogre-recomp.

## Créditos

- **Tradução original: grupo CyberLife.** Todo o trabalho da tradução até a versão 1.0 deste mod é deles:
  textos, fontes e imagens traduzidas. A tradução está há meses sem atualizações, e este mod continua de onde
  ela parou.
- **Continuação:** matheus2x, completando os pontos que ficaram sem tradução.

Se você é do CyberLife e quer que algo nestes créditos mude (nomes, links, como a tradução é citada), abra uma
issue no repositório.

## Como usar

Coloque o `mod-CyberLifePlus-<versão>.zip` da release na pasta `mods/`, ao lado do jogo, e ligue o mod na aba
**Mods** do launcher. Para atualizar, adicione o `.zip` novo pelo launcher: ele apaga a versão antiga e deixa a
nova ligada no lugar dela.

O mod só traz **patches** (`.bps`, só a diferença em relação ao jogo original): ele não contém dados do jogo e
precisa do dump retail, o mesmo que o launcher pede.

## Origem

A versão 1.0 foi feita com `tools/mod.py from-dump --patch` a partir do dump com a tradução do CyberLife
(SHA-1 `a70e9a336f6ea8ff78003938dcd024533f034a68`) sobre o retail
(SHA-1 `9380f1d7ee75e7c6ef24f1b76c88ecdc85088957`): 512 arquivos e 32 overlays. Aplicado ao retail, o mod
dá exatamente os arquivos do dump traduzido.

## Versões

- **1.16:** o mod passa a ser gerado dos textos em `translations/pt-BR/` (no repositório), com o
  `tools/translate.py`. No jogo, o texto é o mesmo da 1.15.
- **1.15:** a abertura: a letra da música do vídeo inicial em português (antes em japonês, com os kanji
  aparecendo como "?").
- **1.14:** as mensagens dos treinos com os status, que estavam em inglês e sumiam (em siglas: CHU, FIS, CON,
  DEF, VEL, RES, COR, porque o espaço só dá 3 letras), "Exp. subiu!" e os tempos da partida (1T, 2T, 1P, 2P).
- **1.13:** os torneios (rodadas, janelas de sair e salvar, eliminação, prêmios) e os nomes das copas, agora
  "Copa Raimon Town", "Copa Fire Tornado"...; os recordes (menu → Sistema → Recordes); e as mensagens que
  estavam em inglês na formação, na loja de equipamento, no ensino de técnicas e no fim da partida.
- **1.12:** as mensagens dos baús e das portas, que estavam em inglês ou pela metade: o que o baú dá
  ("Você ganhou!"), baú vazio, porta trancada, "Parece que uma porta se abriu.", a perda de pontos de garra e
  de amizade e o prêmio do fichário completo.
- **1.11:** o scout, o fichário e o mapa de contatos, que estavam em inglês: a ficha do jogador no fichário
  (como se recruta, onde e a cor da moeda da máquina; "Recrute para ver os dados!"), a busca do scout (Por
  condição, Por nome, posições e atributos), o resultado (Contratar, Buscar outro, Mesma/Mudar condição) e as
  mensagens do mapa de contatos (Recrutável, Já está no time, Liberado!...).
- **1.10:** os menus em texto que estavam em inglês: o menu de campo (Mochila, Formação com História,
  Batalha e Rede 1–3, Dados com Fichário, Celular, Uniformes e Resultados, Sistema com Recordes, Ajuda e
  Opções), o cartão da tela de cima (Nível do time, Amigos, Garra, Amizade), o menu Y dos Amigos (Ordenar,
  Camisa, Dispensar, Equipar, Técnicas, Itens) e a ordenação por Nome, Apelido, Nível e Atributos, os comandos
  da partida (Defesa, Vamos!, Craque, Táticas, Formação), a mochila e o aprender técnicas, com as janelas
  deles ("Precisamos de Endo!", que era "power ofEndo is ok.").
- **1.9:** os menus do time, que estavam em inglês: Amigos (a lista, "Ordem" com os status — Chute, Físico,
  Controle, Defesa, Velocidade, Resistência, Coragem —, Dados, Trocar, Menu, "Número da camisa"),
  Equipamento (Chuteira, Acessório, Luvas, Mudar) e Formação (Montar time - História / Rede 1–3 -, Copiar,
  Abrir), com os botões Fim, Sair, Parar, OK, Sim e Não. Traduzidos do japonês; o CyberLife tinha posto
  "Unsort" no botão que volta à ordem do time e "Club" na lista dos Amigos.
- **1.8:** as telas de comunicação que ainda estavam em inglês: a tela de partida (Procurar rival, Entrar,
  Montar time, Nome e emblema, Trocar uniforme, CPU e campo, Configurar, Jogar agora, os times de rede e o
  "Nível do time"), a tela de download (Partida, Troca, Download, Configurar Wi-Fi), os botões pequenos (Sair,
  Parar, OK, Ver, Cancelar, Sim, Não) e as 16 faixas de título da tela de cima (Partida, Baixar via Wi-Fi,
  Erro de conexão, Superlink 2→3...). Traduzidas do japonês: o CyberLife tinha posto "Pitch & computer" no
  botão de trocar o uniforme e "COM battle" no de escolher o time do CPU e o campo.
  Também os painéis de download (Novos jogadores, "Recrute os baixados pelo mapa de contatos!", Itens e a
  explicação da loja de esportes) e o aviso do Passing na tela de cima.
  E o menu do Desafio Passing (Desafio Passing, Partida, Configurar, os desafios com os dados de rede e com
  dados recebidos, Criar perfil, Registros, Torneio), suas janelas (Iniciar o Passing?, Jogar com este time?,
  os avisos de 11 jogadores e de nome e emblema) e a de "Baixar dados de time".
  Também o cartão de perfil da tela de cima do Passing (Nível do time, Tempo de jogo, Time, Desafios recebidos,
  Recrutados), as faixas de título dele e as janelas de comunicação: Parabéns por zerar o jogo (ligação com o
  Spark/Bomber), Aguarde um pouco, o time mais forte no Monte Magnitude, Erro de conexão, Aguardando o rival,
  Cancelar e Avançar.
  E os 40 títulos do jogador (Onze rival, Pinguim-imperador, Muralha de ferro...), que estavam num inglês com
  erros.
  E as oito páginas do tutorial do Superlink 2→3 e do Link Premium Ogre, que tinham o texto das ilustrações em
  japonês (os rótulos dos consoles, as explicações, a tela de título do outro jogo e os botões).
- **1.7:** as mensagens do menu de slots que estavam em inglês: falha ao carregar e falha ao salvar, a que
  avisa que o jogo vai tentar salvar de novo (que no CyberLife falava de partidas por DS sem fio, outra
  coisa) e os dois avisos de save corrompido ("Os dados salvos do slot estão corrompidos..."). Também a
  coleção: a lista (Vídeo promocional, Ilustrações, Músicas, Personagens, Mapas, Técnicas, Ilustração
  secreta), as abas das músicas (Mapas, Partidas, Eventos) e os botões Tocar, Parar e Sair.
  E o menu "Conectar": Partida, Desafio Passing, Troca, Download, Configurar Wi-Fi, o submenu Download
  (Baixar via Wi-Fi, Baixar via DS sem fio, Baixar dados de time, Superlink 2→3, Link Premium Ogre), os
  botões de Scout Premium e Item Premium e as janelas de aviso ("Sem 11 jogadores no time...").
- **1.6:** os textos das imagens da galeria em português: o botão "Extras" (antes "Gallery") no menu de
  continuar, a lista (Vídeos, Gerar senha, Ligação com celular, Ligação com o filme, Coleção, Surpresa), a
  tela dos vídeos ("Escolha o vídeo que quer ver.", Ver, Sair), a tela de gerar senha e a da ligação com o
  filme (botões, perguntas e Sim/Não). Também o aviso sobre saves alterados que aparece ao carregar o jogo
  ("Alterar os dados salvos com ferramentas de modificação..."), que estava em inglês.
- **1.5:** o que ainda estava em japonês nas tabelas do jogo: os objetivos das minipartidas ("Roube a bola
  do adversário!", agora todos em português, antes metade em inglês), as descrições das quatro técnicas que
  mudam o elemento do jogador (Kaze/Hayashi/Hi/Yama no Gotoshi), os prêmios do mapa dos jogos-treino, os
  times das condições dos jogadores premium e um nome de time do fichário (Kasamino).
  Também os nomes de time e de prêmio dessas tabelas que estavam em inglês ("Neo Japan" → "Novo Japão",
  "Women Selection" → "Seleção Feminina", "Benchers" → "Reservas"), seguindo os nomes do diálogo.
  As descrições das técnicas (lista de técnicas e fichário) e as condições das rotas de batalha em português.
  **Corrige as descrições de itens e táticas da 1.2, que não apareciam no jogo**: a janela só mostrava os
  acentos. Agora elas são gravadas na forma que a janela lê.
- **1.4:** os títulos das cenas da galeria de vídeos (Continuar → Gallery → Game Video) em português: 58
  títulos, com os nomes de personagens e estádios das falas.
  Também os nomes de quem fala em duas partidas que ainda apareciam em japonês (Hibiki, Tae, Ichinose).
  E os menus e mensagens que ficavam no código do jogo: comunicação (DS sem fio, Wi-Fi, trocas, Passing),
  avisos da formação, quiz, scout e itens premium (312 textos).
  Corrige as legendas dos vídeos, que perderam os espaços entre as palavras na 1.3.
- **1.3:** os objetivos da tela de cima (a faixa "Objetivo") em português: 102, uns 80 deles ainda estavam em
  inglês. As letras não ficam mais umas sobre as outras ("aconteceu", "averiguar"): o texto ganha espaçadores
  de 1 px onde o jogo troca de sprite.
- **1.2:** itens e táticas em português: os nomes dos itens (antes em inglês), as descrições de todos os itens
  e as descrições das táticas. Os nomes das técnicas não são traduzidos, mas foram revistos para o romaji
  ("Sabaki no Tettsui" no lugar de "Divine Stamp") e são os mesmos na lista de técnicas e nos cadernos.
- **1.1:** a história fica toda em português: o que faltava dos capítulos 1 a 11, o capítulo 12 (o epílogo com
  o Tenma), as mensagens de campo e das lojas, a máquina de busca de jogadores e os avisos de recrutamento
  (1.268 falas). O que ainda falta está em `docs/TRANSLATION-STATUS.pt-BR.md`, no repositório.
- **1.0:** a tradução do CyberLife, como estava.

## Como continuar a tradução

Este mod é **gerado** de `translations/pt-BR/` no repositório (não edite os arquivos dele à mão): o
`language.json` (nome, versão, autores, as letras acentuadas), os textos em `texts/<grupo>.json` (só o lugar de
cada fala e o português, sem o texto do jogo), as fontes em `font/` (PNG) e o texto das imagens em `images.json`.
O `tools/translate.py` confere tudo e gera o mod sobre a base do CyberLife (`translations/bases/CyberLife/`):

```sh
# o que ainda está em japonês, com o espaço de cada fala (o texto do jogo só vai para build/)
python3 tools/text.py extract boej.pak mods/CyberLifePlus build/text --only 3300

# como um nome ou termo já foi traduzido nas falas prontas
python3 tools/text.py lookup build/text 円堂 ゴッドハンド

# escreva as falas em translations/pt-BR/texts/<grupo>.json, {"evet:33000301:4": "Alô?", ...}, e então:
python3 tools/translate.py check pt-BR
python3 tools/translate.py build pt-BR --install           # suba antes o "version" do language.json
python3 tools/text.py survey boej.pak mods/CyberLifePlus     # quanto já está traduzido
```

As regras do texto (espaço, acentos, quebra de linha, nomes) estão em `docs/TRANSLATION-STATUS.pt-BR.md`. Para traduzir o jogo
para outro idioma a partir deste, veja `docs/TRANSLATING.pt-BR.md`.
