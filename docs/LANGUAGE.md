# In-world language selection (L-167)

## Current scope

Chosen by the maintainer 2026-10-13: change the game's display language while
staying in the same world or server. General settings has a "Game language"
entry opening a list from the game's own supported languages and names.
Apply through vanilla's selection/settings APIs, including its observers
and persistence; never write an option field or keep a controller pointer.
The selected language is Minecraft's setting, not a second Lamium setting.
The maintainer confirms Language is absent from the pause settings on
1.26.51.01. No in-world selection/apply implementation has been built yet.

## Static findings (2026-10-13)

- Mojang's [settings UI sample](https://github.com/Mojang/bedrock-samples/blob/main/resource_pack/ui/settings_screen.json)
  hides the Language tab when not pregame. The sample's current main is not
  runtime proof for 1.26.51.01; the maintainer's observation establishes the
  missing entry on the target build.
- The 26.51.5 client SDK exposes
  `GeneralSettingsScreenController::_chooseLanguage(code, index)` and its
  owned `mLanguages` list. `I18n` exposes `getSupportedLanguageCodes`,
  `getLanguageName`, `getLocaleFor` and `chooseLanguage`.
- `IOptionRegistry` exposes `getLanguage`, `setLanguage` and `saveIfNeeded`.
  Which selection path updates and saves this option, and the required
  call order, are not established by declarations alone.
- `MinecraftGame` is an `I18nObserver`; it has `onLanguageChanged`.
  `FontRepository::onLanguageChanged` and
  `ResourcePackManager::onLanguageChanged` are available. Observe vanilla
  before deciding which entry API Lamium should call; do not invoke all
  observers independently or assume they can run during UI rendering.
- Lamium's ordinary UI reads the current locale when drawing. Cached names
  need separate review: English Search keeps a Localization pointer until
  world exit; schematic entity/material display names can also be retained.
  A successful build does not verify their refresh during play.

## Read-only trace

`xmake f --language_trace=y` enables `features/research/LanguageTrace.cpp`.
It is off in normal builds. Hooks preserve all arguments, original calls
and return values. No language selection, pack removal, packet modification
or preference writes are initiated by Lamium. No assets, packet contents,
server addresses or pack identifiers are dumped.

The trace is limited to 300 log entries per enable. Settings snapshots retain
only strings; list enumeration is limited to 96 entries and runs only when
the owned snapshot changes. It logs:

- General settings' global flag, language entries/selection and resource-tab
  flag, with the current language and saved language option.
- Selection and I18n call begin/end, then game, font and pack observers.
- Pack stack operations as type/count/result (Level=0, Global=1,
  Treatment=2, BaseGame=3). Level does not by itself identify a server pack.
- Received server pack information as required flag/count/add-on/script
  flags, without changing the received information or its handling.

Next runtime checkpoint:

1. With this trace build, use vanilla title-screen Settings > Language to
   switch Japanese to English, then back. Keep the ordinary selection and
   display behavior; collect the `L-167` lines to establish observer order
   and current versus saved language.
2. Enter a local world and inspect pause settings (language absent, as
   reported), and Global Resources' available/read-only controls. Exit and
   restart to verify the selected language persists through vanilla.
3. If convenient, join the maintainer's BDS with a required pack and record
   receipt/stack logs. This step does not disable it or prove removal safe.

No runtime trace evidence has been collected. Do not mark language apply or
server pack disabling as working from these hooks compiling.

## Server-required pack research

The maintainer additionally asked whether locally disabling a required server
resource pack is possible. This is feasibility research only; global pack
editing still waits for language selection, and the existing implementation
boundary excludes changing/skipping world and server packs.

Mojang's [protocol documentation](https://mojang.github.io/bedrock-protocol-docs/1.26.60-preview.30/packets/resource-packs-info-packet/)
describes the required flag as the requirement to apply packs to join. That
page targets a newer preview; the target SDK independently contains
`ResourcePacksInfoPacketPayload::mResourcePackRequired` and the resource-pack
list. An optional decline is not equivalent to disabling a required pack
while keeping the connection.

The target SDK separates Level and Global stacks and exposes
`ResourcePackManager::setStack` and stack composition. This suggests a local
application boundary exists; it does not establish that changing a Level
stack during play reloads meshes, atlases, UI, languages and entity/block
resources safely. A server's pack can supply custom content and UI, so
removing the whole stack can make received gameplay state unrenderable.

Before an implementation choice: distinguish optional packs from required
ones, cosmetic replacements from required content definitions, handshake
behavior from post-join local rendering, and joining without applying from
removing an already-loaded pack. Establish a verified reload/restoration
path in an owned local/BDS environment. Whole-pack versus selected-resource
overrides remains an open product choice. No claim of support yet.
