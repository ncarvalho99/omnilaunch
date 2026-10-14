# OmniLaunch 1.1.1

Hotfix release for OmniLaunch: fixes theme preset scanning and auto-apply after fast return from games, preserves curated catalog ordering in Theme Shop, adds automatic background audio download and playback for video themes, and expands the animated theme store with soundtracks and new content.

If you encounter any bugs, please report them directly to the developer on Telegram at https://t.me/ncarvalho99 with your console logs (`/config/OmniLaunch/menu.log`).

## English

### Theme Shop & Audio
- **Theme Preset Scanning & Auto-Apply**: Fixed an issue where returning from suspended games or applets prevented downloaded themes from auto-applying or appearing in the Installed tab.
- **Curated Catalog Ordering**: Preserved official curated store ordering so featured themes appear at the top of the Theme Shop catalog.
- **Video Themes Audio Support**: Video themes now automatically download and play their accompanying background soundtrack (`media/music/theme.mp3`) alongside video wallpapers.
- **Store Updates**: All 44 animated video themes with sound in the official store now feature extracted high-quality MP3 soundtracks, and the new featured animated theme **The Leper King** is now available.

### Feedback & Bug Reports
- **Contact Developer**: Report issues and send crash logs on Telegram at https://t.me/ncarvalho99.

## Português

### Loja de Temas e Áudio
- **Varredura de Predefinições e Aplicação Automática**: Corrigido um problema em que o retorno rápido de jogos ou applets suspensos impedia a aplicação automática de novos temas ou sua exibição na aba Instalados.
- **Ordem Curada do Catálogo**: A ordem curada da loja oficial agora é mantida, garantindo que os temas em destaque apareçam no topo do catálogo da Loja de Temas.
- **Suporte a Áudio em Temas de Vídeo**: Temas animados em vídeo agora baixam e reproduzem automaticamente sua trilha sonora de fundo (`media/music/theme.mp3`) junto ao papel de parede em vídeo.
- **Atualizações da Loja**: Todos os 44 temas em vídeo com som na loja oficial agora contam com áudio MP3 de alta qualidade, e o novo tema animado em destaque **The Leper King** já está disponível.

### Relato de Bugs e Suporte
- **Contato com o Desenvolvedor**: Envie logs e relate bugs diretamente no Telegram em https://t.me/ncarvalho99.

# OmniLaunch 1.1.0

Feature and maintenance update for OmniLaunch: Flow view gets the look of a physical Nintendo Switch game case, returning to HOME is faster, the launch animation is smoother again, and the update check now follows the OmniLaunch repository. Version 1.0.1 was never published, so its changes are included here.

If you encounter any bugs, please report them directly to the developer on Telegram at https://t.me/ncarvalho99 with your console logs (`/config/OmniLaunch/menu.log`).

## English

### Views & Navigation
- **Game-Case Look in Flow View**: The 3D cases now look like physical Nintendo Switch game cases: a red spine on the left edge, a grey right edge, and a red tab with the Nintendo Switch logo in the top-left corner of the cover.
- **Full-Screen Selection Fixed**: Pressing Down on the D-pad or on either analog stick in the single-row views no longer makes the selection frame cover the whole screen.

### Controls & Interface
- **Sort on Minus (-), View Switch on R**: Changing the game sort order (A-Z, Recent, Favorites, Most Played, Custom Order) is on the Minus button (-), and switching the view mode is on R, with updated on-screen legends.
- **Folders**: Empty slots are shown again in every layout, and pressing Down on the header of an open folder focuses its first icon.

### Performance & Animations
- **Faster Return to HOME**: Coming back to the menu after closing a game or an applet is faster. WaraWara Plaza assets, the large clock font, game metadata reads and user avatars now load on demand or progressively instead of all at startup.
- **Smoother Launch Animation**: The zoom and fade shown when opening a game or homebrew is longer and smoother again (about one second).
- **Game Catalog Stays in Sync**: The game catalog file is rewritten when a title is renamed or its view settings change, not only when titles are added or removed.
- **Silent Menu Start**: The menu no longer plays the system applet start-up sound when it opens.

### Updates & Migration
- **Update Feed**: The update check reads OmniLaunch releases from `ncarvalho99/omnilaunch` and falls back to `ncarvalho99/omnilauncher`. The legacy SwitchU feed is no longer used.
- **Migration Retry**: The one-time SwitchU to OmniLaunch folder migration is only marked as done once the legacy folders are gone, so an interrupted migration is retried on the next boot.

### Feedback & Bug Reports
- **Contact Developer**: Report issues and send crash logs on Telegram at https://t.me/ncarvalho99.

## Português

### Visualizações e Navegação
- **Visual de Caixa de Jogo no Modo Flow**: As capas 3D agora parecem caixas físicas de jogos do Nintendo Switch: lombada vermelha na borda esquerda, lateral direita cinza e uma aba vermelha com o logotipo Nintendo Switch no canto superior esquerdo da capa.
- **Seleção em Tela Cheia Corrigida**: Pressionar para baixo no direcional ou em qualquer analógico nas visualizações de linha única não faz mais a moldura de seleção cobrir a tela inteira.

### Controles e Interface
- **Ordenação no Menos (-), Troca de Visualização no R**: A alteração da ordem dos jogos (A-Z, Recentes, Favoritos, Mais Jogados, Minha Ordem) fica no botão Menos (-), e a troca do modo de visualização fica no R, com legendas atualizadas na tela.
- **Pastas**: Os slots vazios voltam a aparecer em todos os layouts, e pressionar para baixo no cabeçalho de uma pasta aberta foca o primeiro ícone.

### Desempenho e Animações
- **Retorno ao HOME Mais Rápido**: Voltar ao menu depois de fechar um jogo ou applet ficou mais rápido. Os recursos da Praça WaraWara, a fonte grande do relógio, a leitura de metadados dos jogos e os avatares dos usuários agora carregam sob demanda ou de forma progressiva, e não todos na inicialização.
- **Animação de Abertura Mais Suave**: O zoom e o esmaecimento exibidos ao abrir um jogo ou homebrew voltaram a ser mais longos e suaves (cerca de um segundo).
- **Catálogo de Jogos Sempre Atualizado**: O arquivo de catálogo de jogos é reescrito quando um título é renomeado ou suas configurações de visualização mudam, e não apenas quando títulos são adicionados ou removidos.
- **Menu Sem Som de Inicialização**: O menu não toca mais o som de inicialização de applet do sistema ao abrir.

### Atualizações e Migração
- **Fonte de Atualização**: A verificação de atualização lê as versões do OmniLaunch em `ncarvalho99/omnilaunch` e usa `ncarvalho99/omnilauncher` como alternativa. O feed antigo do SwitchU não é mais usado.
- **Migração com Nova Tentativa**: A migração única das pastas do SwitchU para o OmniLaunch só é marcada como concluída quando as pastas antigas não existem mais, então uma migração interrompida é refeita na próxima inicialização.

### Relato de Bugs e Suporte
- **Contato com o Desenvolvedor**: Envie logs e relate bugs diretamente no Telegram em https://t.me/ncarvalho99.
