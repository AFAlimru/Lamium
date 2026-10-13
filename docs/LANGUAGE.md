# In-world language selection (L-167)

## Current scope

Chosen by the maintainer 2026-10-13: change the game's display language while
staying in the same world or server. General settings has a "Game language"
entry opening a list from the game's own supported languages and names.
Apply through vanilla's selection/settings APIs, including its observers
and persistence; never write an option field or keep a controller pointer.
The selected language is Minecraft's setting, not a second Lamium setting.
The maintainer confirms Language is absent from the pause settings on
1.26.51.01.

## Built (2026-10-13), seen in a local world on `9651b4e`

General settings > "Game language" (`ui/LanguageView.cpp`) lists the
languages I18n reports as supported and loaded, with their own names; the
current one is highlighted and also shown on the settings row. Click or
Enter applies; Esc or Cancel returns to the settings list.

`ui/GameLanguage.cpp` queues the apply to the client thread and checks
again there: same language as when asked, same dimension, the settings
screen still open, the gate `Game language (native apply)` allowed, the
code loaded, and no schematic save running (resource changes invalidate its
live readers). It then follows the order vanilla OreUI was seen to use:
`I18n::chooseLanguage`, confirm the current language changed, then
`ResourcePackManager::onLanguageChanged`, then `IOptionRegistry::setLanguage`
and `saveIfNeeded`. Success is read back from I18n and the option, not from
a return value. Before applying, English Search's names, schematic item
names and schematic ghost resources are dropped so they are rebuilt in the
new language.

Seen (VALIDATION-LOG): in-world application, other game text following,
restart persistence. Not separately seen: English Search afterwards,
schematic ghosts after a change, BDS.

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

Baseline checked 2026-10-13 on `e93d154` (VALIDATION-LOG): the maintainer
confirms title-screen Japanese -> English -> Japanese changes display/fonts
normally and Lamium settings opens in the local world. OreUI, not the JSON
controller, performs the selection. The log shows I18n selection -> game
observer -> font update, then the pack-language callback; the saved language
is still old inside the I18n call and is updated by the next selection.
There are 29 supported languages. In-world application and restart
persistence remain unverified. World join reports no required packs and
zero resource packs, so server-required-pack research has no runtime evidence.

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
