# OmniLaunch 1.2.1

Hotfix and ergonomics release for OmniLaunch: configurable view switch button (Minus vs R) to prevent conflicts with Homebrew Menu launching, custom manual background opacity scale and overlay fix, and improved Game Gallery tab layout.

If you encounter any bugs, please report them directly to the developer on Telegram at https://t.me/ncarvalho99 with your console logs (`/config/OmniLaunch/menu.log`).

## English

### Controls & Navigation
- **Configurable View Switch Button**: Added an option in OmniLaunch > Options to set the view cycling button to either **R** or **- (Minus)**. When set to Minus, the sort shortcut automatically swaps to R, keeping R free to hold on game launch for Homebrew Menu access (fixes #1).

### Views & Artwork
- **Custom Background Opacity**: Manually selected game backgrounds now honor the artwork opacity scale setting configured in OmniLaunch > SteamGridDB (fixes #2).
- **Backdrop Overlay Fix**: When a custom game background is active, the automatic SteamGridDB hero backdrop is disabled, preventing double image overlay and visual clutter (fixes #2).
- **Game Gallery Layout**: In the Game Gallery, the "Backgrounds / Heroes" tab is now positioned directly below "Covers / Grids" instead of being pinned to the bottom of the screen.

## Português

### Controles e Navegação
- **Botão de Troca de Vista Configurável**: Adicionada opção em OmniLaunch > Opções para definir o botão de alternância de modos de visualização entre **R** e **- (Minus)**. Ao escolher Minus, o atalho de ordenação inverte automaticamente para R, permitindo segurar R ao abrir jogos para acessar o Homebrew Menu sem trocar de layout acidentalmente (corrige #1).

### Visualizações e Arte
- **Opacidade de Fundo Manual**: Imagens de fundo definidas manualmente agora respeitam o ajuste de opacidade configurado em OmniLaunch > SteamGridDB (corrige #2).
- **Correção de Sobreposição de Fundo**: Quando um jogo possui fundo manual ativo, a imagem hero automática do SteamGridDB é desativada, eliminando a sobreposição de duas imagens (corrige #2).
- **Layout da Galeria de Jogos**: Na Galeria de Jogos, a aba "Fundos" agora fica posicionada logo abaixo de "Capas", em vez de fixada no rodapé da tela.

# OmniLaunch 1.2.0

Major update for OmniLaunch: comprehensive system and hardware stability enhancements, extensive memory and performance optimizations, advanced settings, folder customization with new visual styles and animations, automatic Day/Night theme switching, full docked 1080p scaling, restored smooth launch and resume zoom animations, high-resolution square artwork downloads, and seamless Metro live tiles support.

If you encounter any bugs, please report them directly to the developer on Telegram at https://t.me/ncarvalho99 with your console logs (`/config/OmniLaunch/menu.log`).

## English

### System & Performance
- **Docked 1080p Full-Screen Scaling**: The managed display layer is now created at 1920x1080 when docked, eliminating black borders around the interface.
- **Automatic CPU/GPU Clock Recovery**: Restores APM CPU boost to normal clock speeds upon application exit, keeping GPU clocks at 307.2 MHz and preventing menu render lag.
- **VRAM Optimizations**: Defer offscreen render targets (-18 MB boot VRAM savings), load sound effects on demand, and optimize daemon heap and stack.
- **Streamer Burst**: Temporarily raises decode and upload limits on fast return to HOME so visible tiles populate instantly.
- **Forwarder & HOME Management**: Automatically terminates homebrew forwarders on HOME button press to avoid system hangs. Pressing physical HOME button on the home screen smoothly resumes the currently suspended game.

### Views & Artwork
- **Restored Smooth Launch Zoom**: Re-enabled the classic 0.36s cubic zoom animation with pulse and glowing borders on both cold launch and resume.
- **Metro Live Tiles Enhancements**: 2x1 wide tiles display high-resolution SteamGridDB widescreen banners with title logos, and 2x2 large square tiles render full high-resolution square game icons without cropping distortion.
- **High-Resolution Square Artwork**: SteamGridDB scrape now automatically downloads 1024x1024 square artwork, and IconStreamer decodes native game icons at full 256px resolution.

### Settings & Customization
- **Timezone & Language Pickers**: Searchable interactive dialogs in System settings to change location name and console language with instant filtering.
- **Live Audio Controls**: Real-time speaker auto-mute toggle and TV audio output mode selection in Audio settings.
- **Display & Nickname**: TV underscan slider in Display settings and editable console nickname via on-screen keyboard.
- **Folder Customization**: 6 new folder visual styles and a "Show Cover" toggle to display the leading game's artwork as the folder miniature.
- **Folder Zoom & Wiggle Animations**: Smooth spring zoom when entering and exiting folders, plus gentle icon wiggle animation while reordering tiles in edit mode.
- **Automatic Day/Night Theme**: Automatically switches between dark and light themes based on custom schedules or real-time local sunrise/sunset calculations.
- **Installation Repair & Auto-Backup**: OmniLaunch Manager includes an in-memory font loader and repair feature for Atmosphere pack updates, while the daemon maintains an automatic configuration backup in `/backup/OmniLaunch`.

## Português

### Sistema e Desempenho
- **Escala 1080p Tela Cheia na Dock**: A camada de exibição agora é criada em 1920x1080 ao conectar na dock, eliminando bordas pretas ao redor da interface.
- **Recuperação Automática de Clock**: Redefine o boost de CPU/GPU via APM ao sair de jogos, mantendo a GPU a 307.2 MHz e evitando lentidão no menu.
- **Otimizações de Memória**: Alocação diferida de alvos offscreen (-18 MB de VRAM no boot), decodificação lazy de efeitos sonoros (SFX) e redução de heap/stack do daemon.
- **Aceleração do Streamer**: Eleva temporariamente o limite de decodificação no retorno rápido à HOME para carregar as capas instantaneamente.
- **Gerenciamento de Forwarders e HOME**: Encerra forwarders automaticamente ao pressionar o botão HOME para evitar congelamentos. Pressionar o botão HOME físico na tela inicial retoma suavemente o jogo suspenso.

### Visualizações e Arte
- **Zoom Suave de Lançamento Restaurado**: Reativada a clássica animação de 0.36s com zoom cúbico, pulso e borda brilhante tanto no lançamento frio quanto ao resumir jogos.
- **Melhorias na Vista Metro**: Blocos 2x1 exibem banners widescreen 16:9 do SteamGridDB com logotipo, e blocos 2x2 exibem o ícone quadrado completo em alta resolução sem cortes.
- **Artes Quadradas em Alta Resolução**: A busca do SteamGridDB agora baixa automaticamente artes quadradas de 1024x1024, e o IconStreamer decodifica ícones nativos na resolução total de 256px.

### Configurações e Personalização
- **Seletores de Fuso Horário e Idioma**: Caixas de diálogo interativas pesquisáveis nas configurações do sistema para trocar localização e idioma com filtro instantâneo.
- **Controles de Áudio ao Vivo**: Silenciamento automático de alto-falantes e seleção de saída de áudio de TV nas configurações de áudio.
- **Tela e Apelido**: Controle deslizante de underscan de TV e edição do apelido do console pelo teclado.
- **Personalização de Pastas**: 6 novos estilos visuais de pasta e opção "Show Cover" para usar a capa do jogo principal na miniatura.
- **Animações de Zoom e Balanço**: Abertura e fechamento de pastas com zoom suave e leve balanço dos ícones ao reordenar em modo de edição.
- **Tema Automático Dia/Noite**: Troca automática de temas com base em horários definidos ou cálculo de nascer/pôr do sol local.
- **Reparo de Instalação e Auto-Backup**: O OmniLaunch Manager agora conta com carregador de fontes em memória e botão de reparo de instalação, e o daemon mantém backup automático das configurações em `/backup/OmniLaunch`.

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
