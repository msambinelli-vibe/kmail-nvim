# KMail Vim Navigation

Plugin para o KMail 6 que usa atalhos no estilo Vim na lista e no visualizador
de mensagens:

- `j`: próxima mensagem;
- `k`: mensagem anterior;
- `Shift+j`: rolar a mensagem visualizada uma página para baixo;
- `Shift+k`: rolar a mensagem visualizada uma página para cima;
- `gg`: primeira mensagem da lista;
- `G`: última mensagem da lista;
- `Espaço`: alternar a tag `selected` na mensagem atual ou nas mensagens
  selecionadas com o mouse e avançar para a próxima mensagem após a confirmação;
- `d`: alternar a tag `deleted` nas mensagens marcadas com `selected`;
- `a`: alternar a tag `archived` nas mensagens marcadas com `selected`;
- `s`: alternar a tag `spam` nas mensagens marcadas com `selected`;
- `c`: remover `selected` de todas as mensagens exibidas na lista;
- `C`: remover `selected`, `archived`, `deleted` e `spam` de todas as mensagens
  exibidas na lista;
- `u`: desfazer a última alternância de tag de ação feita pelo plugin;
- `S`: executar as ações pendentes em todas as mensagens da pasta atual.
- `gf`: criar um filtro rápido a partir da mensagem atual.

Os atalhos originais do KMail (`N`/→ e `P`/←) continuam funcionando. O plugin
reaproveita as ações nativas do KMail, portanto a seleção, a visualização da
mensagem e o tratamento de conversas continuam a cargo do próprio aplicativo.
Como `j`, `a`, `d`, `s`, `c`, `C`, `gf` e `Espaço` são reservados pelo plugin, os
atalhos de uma tecla conflitantes do KMail são removidos; as ações
correspondentes continuam disponíveis nos menus. **Ir para pasta** também
recebe `Ctrl+Shift+j`.

## Filtros rápidos

`gf` abre um assistente Qt operável integralmente pelo teclado. A primeira tela
oferece condições extraídas da mensagem atual — `List-ID` (ou outro cabeçalho
de lista reconhecido pelo próprio KMail), endereço e domínio do remetente e
assunto. É possível marcar várias condições, combinadas com **AND**, e editar
qualquer valor antes de continuar.

Na segunda tela, escolha a intenção que o filtro adicionará às mensagens:
`deleted`, `spam` ou `archived`. O filtro não exclui nem move mensagens
imediatamente. Ele apenas adiciona a tag correspondente; a revisão visual e a
operação efetiva continuam sendo feitas com `S`.

Na última tela, o filtro pode ser aplicado somente às próximas mensagens,
também à mensagem atual ou retroativamente a todas as correspondências da pasta
atual. A prévia mostra a quantidade e uma lista paginada antes da confirmação.
Independentemente dessa escolha, a nova regra fica ativa para mensagens futuras
em todas as pastas reconhecidas como Inbox, em todas as contas.

Teclas do assistente:

- `j`/`k`: navegar;
- `Espaço`: marcar uma condição ou escolher uma opção;
- `Tab`: marcar a condição e avançar para a próxima;
- `e`: editar o valor da condição na primeira tela;
- `Enter`: confirmar a edição, avançar ou criar o filtro;
- `Esc`/`q`: voltar; na primeira tela, cancelar;
- `Ctrl+d`/`Ctrl+u`: avançar ou voltar uma página da prévia.

As regras são gravadas pelo gerenciador nativo de filtros do KMail e inseridas
antes das regras existentes, mas sem interromper o processamento posterior. O
assistente se recusa a salvar enquanto o gerenciador nativo de filtros estiver
aberto, evitando que uma cópia antiga da configuração sobrescreva a regra nova.
O agente local do KMail aplica filtros após o recebimento/sincronização; por uma
limitação do próprio agente, mensagens que já chegam com estado lido, spam ou
ignorado podem não passar pelo fluxo automático.

## Tags e aplicação

Ao ser habilitado, o plugin garante que as tags `selected`, `deleted`,
`archived` e `spam` existam no Akonadi e as registra entre as tags exibidas pelo
KMail. Elas podem ser personalizadas em **Configurações → Configurar o KMail →
Aparência → Tags de mensagens**. Preserve esses quatro nomes, pois eles fazem
parte do fluxo do plugin. Se essa tela já estava aberta durante a inicialização,
feche-a e abra-a novamente para recarregar a lista.

Tags criadas por versões anteriores no formato imutável do Akonadi são
atualizadas no próprio ID para o formato editável usado pelo KMail; as
associações existentes com mensagens são preservadas.

`selected` funciona como uma seleção persistente dentro da lista atual. Ao usar
`d`, `a` ou `s`, o plugin processa todas as mensagens visíveis dessa lista que
possuem `selected`. Se não houver nenhuma, ele primeiro atribui `selected` à
seleção feita com o mouse — ou à mensagem atual — e então executa o comando. Em
caso de sucesso, `selected` é removida do lote; em caso de erro, é preservada
para permitir uma nova tentativa.

`c` e `C` respeitam a lista atualmente exibida, incluindo mensagens dentro de
conversas recolhidas e excluindo linhas ocultas pelo filtro rápido. Além das
quatro tags, `C` neutraliza marcas de exclusão incompletas deixadas por versões
antigas do plugin.

O comando `u` desfaz a última alternância de `deleted`, `archived` ou `spam`;
alternar `selected` com `Espaço` não substitui esse histórico de undo.

Os comandos `d`, `a` e `s` alternam apenas a respectiva tag de ação. Isso
permite revisar as mensagens antes de executar `S`. Ao aplicar:

- `deleted` move a mensagem para a lixeira da respectiva conta configurada no
  KMail;
- `spam` atribui o estado de spam/junk do KMail e move a mensagem para a pasta
  Spam da respectiva conta, com fallback para a pasta Spam padrão do KMail;
- `archived` move a mensagem segundo a configuração de arquivamento da conta no
  KMail, incluindo pastas anuais ou mensais.

Depois de uma ação bem-sucedida, as tags de fluxo são removidas. Se uma mensagem
tiver várias delas, a precedência é `deleted`, depois `spam`, depois `archived`,
para que ela não seja movida duas vezes. O `S` procura essas tags em todas as
mensagens da pasta atual, independentemente da seleção persistente, da seleção
feita com o mouse e do filtro rápido da lista.

O plugin só remove as tags depois de confirmar a operação no Akonadi. Se não
houver uma lixeira configurada, a mensagem permanece onde está e `deleted` é
preservada. Marcadores incompletos deixados pela implementação antiga também
são reconhecidos e corrigidos na próxima execução de `S`.

Da mesma forma, se a conta não tiver uma pasta Spam configurada, a mensagem não
é movida e a tag `spam` é preservada para uma nova tentativa.

## Requisitos

- KMail 6 / KDE PIM 6;
- Qt 6;
- Extra CMake Modules (ECM) 6.22 ou superior;
- KDE Frameworks 6: CoreAddons e XmlGui;
- KDE Frameworks 6: ConfigCore;
- bibliotecas de desenvolvimento Akonadi, AkonadiMime, MessageViewer, PimCommon
  PimCommon, PimCommonAkonadi e MailCommon;
- CMake 3.20 ou superior e um compilador C++17.

No Arch Linux, as bibliotecas necessárias são fornecidas pelos pacotes de
desenvolvimento já incluídos em `kmail`, `mailcommon`, `pimcommon`,
`kcoreaddons`, `kxmlgui` e `qt6-base`; instale também
`extra-cmake-modules`.

## Compilar e testar

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

## Instalar

O KMail procura plugins de visualização principal em
`<diretório de plugins do Qt>/pim6/kmail/mainview`. Em instalações Linux que
usam `/usr/lib/qt6/plugins`, instale assim:

```sh
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build
sudo cmake --install build
```

Reinicie o KMail. O plugin aparece em **Configurações → Configurar o KMail →
Plugins** como **Navegação de mensagens no estilo Vim** e vem habilitado por
padrão.

Para uma instalação local sem `sudo`, use um prefixo do usuário e inclua o
diretório na busca de plugins do Qt antes de iniciar o KMail:

```sh
cmake --install build --prefix "$HOME/.local"
QT_PLUGIN_PATH="$HOME/.local/lib/qt6/plugins${QT_PLUGIN_PATH:+:$QT_PLUGIN_PATH}" kmail
```

## Pacote Arch Linux

O [`PKGBUILD`](PKGBUILD) gera um pacote instalável e também executa os testes:

```sh
makepkg -si
```

O pacote segue a convenção VCS do Arch: chama-se
`kmail-vim-navigation-git`, clona a branch `master` do repositório definido em
`url` e calcula `pkgver` a partir do número e do hash do commit. O checksum
`SKIP` é intencional para uma fonte Git.

## Compatibilidade

O plugin usa a interface de plugins genéricos do KDE PIM 6 e os identificadores
de ação `go_next_message`, `go_prev_message` e `jump_to_folder`. Ele foi criado e
testado para a série KMail 26.04 / Qt 6.

## Licença

Código distribuído sob `GPL-2.0-or-later`; arquivos de build sob `BSD-3-Clause`.
