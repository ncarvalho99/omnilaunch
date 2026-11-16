# OmniLaunch 1.3.2

Update for OmniLaunch 1.3.1. Docked clocks stay docked after a game (issue #5), a new OmniLaunch tab to change the look of any theme: text colour (issue #6), solid window colour and corner roundness, an icon store with icon packs that switch live, and icons that are ready on every page of HOME.

If you encounter any bugs, please report them directly to the developer on Telegram at https://t.me/ncarvalho99 with your console logs (`/config/OmniLaunch/menu.log`).

## English

### Docked performance (issue #5)
- Leaving a game, port or homebrew while docked no longer switches the console to handheld clocks. The docked performance mode is kept when a title exits and when HOME is ready, so the next game runs at docked clocks again.

### Theme look: Theme Shop > OmniLaunch
- **Text colour** (issue #6): turn on a custom colour for titles, labels and hints, with hue, saturation and brightness sliders or a typed code such as `#FFD54F`, for text that is hard to read over a theme.
- **Solid window colour**: windows, side buttons, the top buttons and the button hints take one flat colour instead of the theme's glass. Opaque black is the lightest choice for the console and suits OLED screens.
- **Glass sharpness** moved here. Below 100 % opacity, the solid colour blurs what shows through it by this amount.
- **Corner roundness** from 0 % (square) to 100 %, for windows, buttons, icons, battery rings and the selection frame together.
- **Use the theme's look** turns the three back to the theme's own glass, corners and text.
- The other rows that were on this tab moved to the Options tab.

### Icon store
- New **Icons** tab in the Theme Shop: download icon packs that replace the menu's icons (sidebar, top buttons, battery, arrows and more) and switch them without restarting.
- Installed packs show an **Update** badge when a new version is published.
- The **Themes** tab holds both catalogues behind an Animated / Static switch.
- Catalogue previews are kept on the SD card, so reopening the shop does not download them again.

### Icons on HOME (issue #4)
- Classic keeps the icons of the page you see and the pages beside it, and loads the next page before you reach it: the second page no longer opens on loading spinners after a style switch or a game.
- Switching views with Minus reuses the icons already loaded.

### Music player
- A held touch on the open player no longer drags icons on the HOME page.
- Classic draws the player with its own frosted glass.
- Downloaded songs keep accented and Japanese, Chinese or Korean letters in their names; styled letters are drawn as plain letters instead of boxes.
- The music button looks like the buttons beside it; a small dot shows that music is playing.
- Theme songs are listed only under Themes.

### Top bar (Classic)
- The top bar shows one account: the first one, or the one you chose. Press it to open the account picker with every account and the + tile to create one; press X on an account there to show it in the top bar and start games with it.
- The button hints in the lower right corner take the solid window colour too.

## Português

### Desempenho no dock (issue #5)
- Sair de um jogo, port ou homebrew com o console no dock não muda mais os clocks para o modo portátil. O modo de desempenho do dock é mantido quando o título fecha e quando o HOME fica pronto, então o próximo jogo volta a rodar com os clocks do dock.

### Visual do tema: Loja de Temas > OmniLaunch
- **Cor do texto** (issue #6): ative uma cor própria para títulos, rótulos e dicas, com controles de matiz, saturação e brilho ou um código digitado como `#FFD54F`, para textos difíceis de ler sobre um tema.
- **Cor sólida das janelas**: janelas, botões laterais, os botões do topo e as dicas de botões usam uma cor lisa em vez do vidro do tema. Preto opaco é a opção mais leve para o console e combina com telas OLED.
- **Nitidez do vidro** veio para cá. Abaixo de 100 % de opacidade, a cor sólida desfoca o que aparece por trás dela nessa medida.
- **Arredondamento dos cantos** de 0 % (quadrado) a 100 %, para janelas, botões, ícones, anéis de bateria e a moldura de seleção juntos.
- **Usar o visual do tema** volta as três opções para o vidro, os cantos e o texto do próprio tema.
- As outras opções que ficavam nesta aba foram para a aba Opções.

### Loja de ícones
- Nova aba **Ícones** na Loja de Temas: baixe pacotes de ícones que substituem os ícones do menu (barra lateral, botões do topo, bateria, setas e mais) e troque sem reiniciar.
- Pacotes instalados mostram o selo **Atualizar** quando sai uma versão nova.
- A aba **Temas** reúne os dois catálogos com a chave Animados / Estáticos.
- As prévias do catálogo ficam guardadas no cartão SD, então reabrir a loja não baixa tudo de novo.

### Ícones no HOME (issue #4)
- O Classic guarda os ícones da página vista e das páginas ao lado, e carrega a próxima página antes de você chegar nela: a segunda página não abre mais com ícones carregando depois de trocar de estilo ou sair de um jogo.
- Trocar de visualização com Menos reaproveita os ícones já carregados.

### Player de música
- Um toque segurado no player aberto não arrasta mais os ícones do HOME.
- O Classic desenha o player com o próprio vidro fosco.
- Músicas baixadas mantêm acentos e letras em japonês, chinês ou coreano no nome; letras estilizadas aparecem como letras normais em vez de quadrados.
- O botão de música tem o mesmo visual dos botões ao lado; um pontinho indica que há música tocando.
- As músicas de tema aparecem só em Temas.

### Barra superior (Classic)
- A barra superior mostra uma conta só: a primeira, ou a que você escolher. Aperte nela para abrir o seletor com todas as contas e o bloco + para criar uma; aperte X numa conta para mostrá-la na barra e abrir os jogos com ela.
- As dicas de botões no canto inferior direito também usam a cor sólida das janelas.

# OmniLaunch 1.3.1

Update for OmniLaunch 1.3.0. One music player for Classic and Cards with a list of every theme's music, long song titles that scroll instead of being cut, faster and more reliable music downloads, text in Japanese, Chinese, Korean and emoji, and icons and covers that are already there when HOME opens.

If you encounter any bugs, please report them directly to the developer on Telegram at https://t.me/ncarvalho99 with your console logs (`/config/OmniLaunch/menu.log`).

## English

### Music player
- **One player for both styles**: Classic now opens the same player as Cards, so the song shown is always the one playing.
- **Two lists**: Playlist (your downloaded songs and the current theme's music) and Themes (the music of every installed theme). Switch with L and R.
- Theme songs are named after their theme instead of "theme".
- **Long titles scroll**: the playing song and the selected song show their whole name in a moving line instead of being cut with "...". Works for theme names and downloaded songs.
- X plays or pauses; on a downloaded song in Playlist, press X twice to delete it. Y changes the playlist order (SD songs first, theme music first, only one of them).
- In Classic, L inside the player goes back to Playlist and no longer opens Discord.
- Theme music stays in the playlist next to your SD songs.

### Music downloads
- Downloads from the YouTube tab are prepared on the OmniLaunch server and then sent to the console, which is faster and works again for videos that failed before.
- A new search shows a loading panel instead of the previous results.

### Text
- Japanese, Chinese and Korean titles and emoji (monochrome) are drawn with the console's own fonts instead of boxes.

### Icons on HOME (issue #4)
- **Classic**: the icons of the last page you saw are saved and appear in the first frames after a game or a style switch, then are checked against their source.
- **Cards**: every page (Games, Homebrew, Nintendo, Media) keeps its icons and covers the same way, so cards open with their cover instead of a small square icon that grows into it.

### Known issues
- Rarely (about 1 in 28 cold boots) the menu restarts once during start-up; it recovers by itself.
- USB file transfer (MTP) may not be detected through some USB hubs; connect the console directly to the computer.
- A music download that is being prepared cannot be cancelled yet.

## Português

### Player de música
- **Um player para os dois estilos**: o Classic agora abre o mesmo player do Cards, então a música mostrada é sempre a que está tocando.
- **Duas listas**: Playlist (suas músicas baixadas e a música do tema atual) e Temas (a música de todos os temas instalados). Troque com L e R.
- As músicas de tema levam o nome do tema em vez de "theme".
- **Títulos longos rolam**: a música tocando e a selecionada mostram o nome inteiro numa linha que se move, em vez de cortar com "...". Vale para nomes de temas e músicas baixadas.
- X toca ou pausa; numa música baixada da Playlist, aperte X duas vezes para apagá-la. Y muda a ordem da playlist (SD primeiro, tema primeiro, só uma delas).
- No Classic, o L dentro do player volta para a Playlist e não abre mais o Discord.
- A música do tema continua na playlist junto das suas músicas do SD.

### Download de músicas
- Os downloads da aba YouTube são preparados no servidor do OmniLaunch e depois enviados ao console, o que é mais rápido e voltou a funcionar para vídeos que falhavam.
- Uma nova busca mostra um painel de carregamento em vez dos resultados anteriores.

### Texto
- Títulos em japonês, chinês e coreano e emojis (monocromáticos) aparecem com as fontes do próprio console em vez de quadrados.

### Ícones no HOME (issue #4)
- **Classic**: os ícones da última página vista ficam salvos e aparecem nos primeiros quadros depois de um jogo ou de uma troca de estilo, e depois são conferidos com a origem.
- **Cards**: todas as páginas (Jogos, Homebrew, Nintendo, Mídia) guardam ícones e capas do mesmo jeito, então os cartões abrem já com a capa em vez de um ícone pequeno que cresce até ela.

### Problemas conhecidos
- Raramente (cerca de 1 em 28 boots a frio) o menu reinicia uma vez durante a inicialização; ele se recupera sozinho.
- A transferência USB (MTP) pode não ser detectada através de alguns hubs USB; conecte o console direto no computador.
- Um download de música em preparação ainda não pode ser cancelado.
# OmniLaunch 1.3.0

Feature release for OmniLaunch. It adds a second HOME style, **Cards**, based on the CardsNX view of HomeNX by ad3333m. Hold Minus to switch between Classic and Cards at any time. It also adds homebrew launching from HOME, Discord, News, a homebrew updater, the BrowseNX browser, optional 1080p on the TV, new Classic icons and many settings shared by both styles. Classic stays the default.

If you encounter any bugs, please report them directly to the developer on Telegram at https://t.me/ncarvalho99 with your console logs (`/config/OmniLaunch/menu.log`).

## English

### Two HOME styles
- **Cards**: card row with Games, Homebrew, Nintendo and Media pages (ZL/ZR), page dots, Move mode, sorting, per-page order and visibility, card options per software type, achievements and a welcome tour.
- **Live switch**: hold Minus for about half a second to go from Classic to Cards and back, without restarting. Music, settings, users and the suspended game are shared.

### Cards
- Music player (vinyl, playlist, 16-band spectrum, shuffle and repeat that are saved), News, notifications, users, quick settings with a mini player, Bluetooth and Hekate, and a power menu with restart to Hekate.
- Settings with ten sections: background picture and shuffle, TV resolution, vibration, input test, date and time zone, Bluetooth audio devices, MTP, language, sounds, lock screen with password, updates and more. Long descriptions wrap, hints use button glyphs.
- Panel button cycles None, News or a pinned app in both directions; Nextendo, title logos and Bluetooth are switches.
- Glass styles, accent colours, haze, background motion; art from GameTDB and SteamGridDB with batch download.

### Classic
- New "minimal neutral" icon pack, including a 3D battery with charge level and charging bolt, a new notification bell, Nextendo and browser icons.
- ThemeShop > OmniLaunch: Ask who's playing, repeat song, USB file transfer (MTP), lock screen on/off (always on with a password), music on the lock screen, lock now, lock password, 1080p on the TV, in-game Discord messages, icon size and side button size (80-130 %), and a sidebar order list you can show or hide.
- Homebrew and System launcher widgets (open, update, delete .nro; console applets), notification bell, users picker and Social screen with button glyphs.
- One-row views fit the whole top bar; wide covers are cropped to the case in 3D views.

### Homebrew, browser and updates
- .nro apps open in full application mode from both styles; your `hbmenu.nro` is kept aside and restored. A running homebrew is marked on its tile.
- Homebrew updater (Homebrew App Store and GitHub releases).
- **BrowseNX**: downloaded on first use with progress and Cancel, then opened or resumed.

### Social and network
- **Discord**: QR code or token sign-in, servers, channels, direct messages, calls, link previews, attachments and notifications.
- **In-game Discord messages (experimental)**: direct messages appear as Ultrahand notifications while you play, checked every 2 to 4 minutes. Off by default; uses your account token.
- **News** with read tracking; HTTPS with bundled root certificates, connection and speed test, Nextendo player counts.

### Display and system
- **1080p on the TV** (optional, docked): sharper picture, uses more memory, applies on the next HOME start.
- With Ask who's playing off, games start as the HOME user without a picker.
- Smoother text and overlays, decoded icon cache for faster Classic icons, a text cache that never refuses new text, and a watchdog that gives the screen back to a suspended game when the menu fails to start.
- Translations for every supported language.

### Known issues
- Rarely (about 1 in 28 cold boots) the menu restarts once during start-up; it recovers by itself.
- USB file transfer (MTP) may not be detected through some USB hubs; connect the console directly to the computer.

### Credits
- Cards is based on HomeNX / CardsNX by **ad3333m**, who also wrote BrowseNX. Credits are in Settings > About and in the README.

## Português

### Dois estilos de HOME
- **Cards**: fileira de cartões com as páginas Jogos, Homebrew, Nintendo e Mídia (ZL/ZR), pontos de página, modo Mover, ordenação, ordem e visibilidade por página, opções por tipo de software, conquistas e tour de boas-vindas.
- **Troca ao vivo**: segure Menos por cerca de meio segundo para ir do Classic ao Cards e voltar, sem reiniciar. Música, configurações, usuários e o jogo suspenso são compartilhados.

### Cards
- Player de música (vinil, playlist, espectro de 16 faixas, aleatório e repetir salvos), Notícias, notificações, usuários, ajustes rápidos com mini player, Bluetooth e Hekate, e menu de energia com reiniciar no Hekate.
- Configurações com dez seções: imagem de fundo e fundo aleatório, resolução da TV, vibração, teste de controles, data e fuso horário, dispositivos de áudio Bluetooth, MTP, idioma, sons, tela de bloqueio com senha, atualizações e mais. Descrições longas quebram linha e as dicas usam símbolos dos botões.
- Botão do painel alterna entre Nenhum, Notícias ou um app fixado nas duas direções; Nextendo, logos dos títulos e Bluetooth viraram chaves.
- Estilos de vidro, cores de destaque, névoa e movimento do fundo; artes da GameTDB e do SteamGridDB com download em lote.

### Classic
- Novo pacote de ícones "minimal neutral", com bateria 3D com nível de carga e raio ao carregar, novo sino de notificações e ícones do Nextendo e do navegador.
- ThemeShop > OmniLaunch: Perguntar quem está jogando, repetir música, transferência USB (MTP), tela de bloqueio liga/desliga (sempre ligada com senha), música na tela de bloqueio, bloquear agora, senha, 1080p na TV, mensagens do Discord durante jogos, tamanho dos ícones e dos botões laterais (80-130 %) e a lista de ordem da barra lateral, que pode ser mostrada ou escondida.
- Widgets de Homebrew e Sistema (abrir, atualizar e apagar .nro; applets do console), sino de notificações, seletor de usuários e tela Social com símbolos dos botões.
- As visualizações de uma linha encaixam toda a barra do topo; capas largas são recortadas no formato da caixa nas visualizações 3D.

### Homebrew, navegador e atualizações
- Apps .nro abrem em modo de aplicativo completo nos dois estilos; o seu `hbmenu.nro` fica guardado e é restaurado. O homebrew aberto fica marcado no seu bloco.
- Atualizador de homebrew (Homebrew App Store e releases do GitHub).
- **BrowseNX**: baixado no primeiro uso com progresso e Cancelar, depois aberto ou retomado.

### Social e rede
- **Discord**: login por QR code ou token, servidores, canais, mensagens diretas, chamadas, prévias de links, anexos e notificações.
- **Mensagens do Discord durante jogos (experimental)**: mensagens diretas aparecem como notificações do Ultrahand enquanto você joga, verificadas a cada 2 a 4 minutos. Desligado por padrão; usa o token da sua conta.
- **Notícias** com marcação de lidas; HTTPS com certificados raiz incluídos, teste de conexão e de velocidade, contagem de jogadores do Nextendo.

### Tela e sistema
- **1080p na TV** (opcional, no dock): imagem mais nítida, usa mais memória, vale a partir do próximo início do HOME.
- Com Perguntar quem está jogando desligado, os jogos abrem com o usuário do HOME, sem seleção.
- Texto e sobreposições mais suaves, cache de ícones decodificados para o Classic abrir mais rápido, cache de texto que não recusa textos novos e watchdog que devolve a tela ao jogo suspenso quando o menu não inicia.
- Traduções para todos os idiomas suportados.

### Problemas conhecidos
- Raramente (cerca de 1 em 28 inicializações a frio) o menu reinicia uma vez durante a abertura; ele se recupera sozinho.
- A transferência USB (MTP) pode não ser detectada através de alguns hubs USB; conecte o console direto no computador.

### Créditos
- O Cards é baseado no HomeNX / CardsNX de **ad3333m**, que também criou o BrowseNX. Os créditos estão em Configurações > Sobre e no README.
# OmniLaunch 1.2.2

Hotfix release for OmniLaunch: unified manual and SteamGridDB game backdrop rendering, instant backdrop loading via pre-decoded raw cache, neighbor title prefetching for manual backgrounds, and identical opacity scaling (fixes #3).

If you encounter any bugs, please report them directly to the developer on Telegram at https://t.me/ncarvalho99 with your console logs (`/config/OmniLaunch/menu.log`).

## English

### Views & Artwork
- **Unified Backdrop Pipeline**: Custom manual game backgrounds are now handled directly inside the core `SteamGridDbBackdrop` pipeline instead of a separate legacy widget. This eliminates any disparity in opacity, shaders, or color between manual and automatic backgrounds (fixes #3).
- **Instant Raw Backdrop Caching**: Manual backgrounds now generate and utilize fast `.1280x720.rgba-cache` files on the SD card (pre-generated immediately upon selecting an image in Game Gallery), reducing SD read latency from ~500ms to under 10ms (fixes #3).
- **Neighbor Title Prefetching**: Games with custom backgrounds now participate in the home grid prefetch queue, loading adjacent titles into RAM before navigation occurs for instant, stutter-free transitions.
- **Smooth Crossfades**: Custom backgrounds now transition with the same smooth 0.52s cubic crossfade curve as automatic artwork.

## Português

### Visualizações e Arte
- **Pipeline Unificado de Fundos**: Imagens de fundo manuais agora são gerenciadas diretamente pelo renderizador principal `SteamGridDbBackdrop`, em vez de um widget legado separado. Isso elimina qualquer diferença de opacidade, shaders ou tonalidade entre fundos manuais e automáticos (corrige #3).
- **Carregamento Instantâneo via Cache Bruto**: Fundos manuais agora geram e utilizam cache binário `.1280x720.rgba-cache` no cartão SD (criado em segundo plano no momento da escolha na Galeria), reduzindo o tempo de carregamento de ~500ms para menos de 10ms (corrige #3).
- **Pré-carregamento de Vizinhos**: Jogos com fundos customizados agora entram na fila de prefetch da tela inicial, carregando títulos adjacentes na RAM antes da navegação para transições instantâneas e sem engasgos.
- **Transições Suaves em Crossfade**: Fundos manuais agora alternam com a mesma curva cúbica suave de crossfade de 0.52s das artes automáticas.

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
