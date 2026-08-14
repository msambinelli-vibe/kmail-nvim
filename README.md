# KMail Vim Navigation

Plugin para o KMail 6 que usa atalhos no estilo Vim na lista e no visualizador
de mensagens:

- `j`: próxima mensagem;
- `k`: mensagem anterior;
- `Shift+j`: rolar a mensagem visualizada uma página para baixo;
- `Shift+k`: rolar a mensagem visualizada uma página para cima;
- `gg`: primeira mensagem da lista;
- `G`: última mensagem da lista;
- `dd`: atribuir a tag `deleted` à mensagem atual ou às mensagens selecionadas;
- `a`: atribuir a tag `archived` à mensagem atual ou às mensagens selecionadas;
- `s`: atribuir a tag `spam` à mensagem atual ou às mensagens selecionadas;
- `u`: desfazer a última atribuição de tag feita pelo plugin;
- `S`: executar as ações pendentes nas mensagens selecionadas.

Os atalhos originais do KMail (`N`/→ e `P`/←) continuam funcionando. O plugin
reaproveita as ações nativas do KMail, portanto a seleção, a visualização da
mensagem e o tratamento de conversas continuam a cargo do próprio aplicativo.
Como `j`, `a` e `s` são reservados pelo plugin, os atalhos de uma tecla que o
KMail atribui a **Ir para pasta**, **Responder a todos** e **Pesquisar
mensagens** são removidos; essas ações continuam disponíveis nos menus. **Ir
para pasta** também recebe `Ctrl+Shift+j`.

## Tags e aplicação

Os comandos `dd`, `a` e `s` apenas marcam as mensagens. Isso permite revisar a
seleção antes de executar `S`. Ao aplicar:

- `deleted` move a mensagem para a lixeira configurada pelo Akonadi;
- `spam` atribui o estado de spam/junk do KMail;
- `archived` move a mensagem segundo a configuração de arquivamento da conta no
  KMail, incluindo pastas anuais ou mensais.

Depois de uma ação bem-sucedida, as tags de fluxo são removidas. Se uma mensagem
tiver várias delas, a precedência é `deleted`, depois `spam`, depois `archived`,
para que ela não seja movida duas vezes. O `S` atua somente sobre a mensagem
atual ou sobre as mensagens selecionadas; ele não processa silenciosamente a
caixa inteira.

## Requisitos

- KMail 6 / KDE PIM 6;
- Qt 6;
- Extra CMake Modules (ECM) 6.22 ou superior;
- KDE Frameworks 6: CoreAddons e XmlGui;
- KDE Frameworks 6: ConfigCore;
- bibliotecas de desenvolvimento Akonadi, AkonadiMime, MessageViewer, PimCommon
  e PimCommonAkonadi;
- CMake 3.20 ou superior e um compilador C++17.

No Arch Linux, as bibliotecas necessárias são fornecidas pelos pacotes de
desenvolvimento já incluídos em `kmail`, `pimcommon`, `kcoreaddons`, `kxmlgui` e
`qt6-base`; instale também `extra-cmake-modules`.

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
