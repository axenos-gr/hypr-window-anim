# hypr-window-anim

Шейдерные анимации закрытия и открытия окон для Hyprland в стиле Burn-My-Windows (по умолчанию «TV Glitch»).
Пока играет эффект закрытия, раскладка ждёт: соседние окна занимают освободившееся место только после конца анимации.

Состав:
- `hypr-window-anim.so` — плагин Hyprland: перехват закрытия, отрисовка эффектов;
- `hypr-window-anim` — CLI: загрузка с проверкой ABI, перезагрузка конфига, превью, проверка шейдеров.
- `hwa-sheet` (необязательно, `-DHWA_BUILD_TOOLS=ON`) — офлайн-рендерер эффектов для их разработки.

Проверено на Hyprland 0.56.2 (рендерер OpenGL).

## Как это работает

**Закрытие через WM** (`killactive`, `closewindow`, панели задач, жест тачпада, средний клик по group-bar). Все такие запросы проходят через `CHyprXWaylandManager::sendCloseWindow`, плагин перехватывает этот вызов:
1. Снимает снимок окна (с рамкой, тенью и скруглением).
2. Прячет окно через `setHidden`: место в раскладке остаётся за ним, ввод в окно больше не идёт.
3. Сразу передаёт фокус соседнему окну, поэтому несколько Super+C подряд закрывают окна по очереди.
4. Проигрывает эффект поверх снимка.
5. Отправляет приложению настоящий запрос на закрытие. Hyprland снимает окно с экрана и перестраивает раскладку штатно.

Если приложение не закрылось (диалог «сохранить изменения?» или просто отказ), окно возвращается: сразу, если появился его диалог, иначе через `close_timeout_ms`.

**Закрытие самим приложением** (кнопка в заголовке, Ctrl+Q, `exit`, падение). Эффект играет на снимке, который делает сам Hyprland, вместо штатного fade-out. Раскладка перестраивается сразу: узнать о таком закрытии заранее невозможно.

**Открытие.** Когда окно появилось, плагин мгновенно завершает встроенные `windowsIn`/`fadeIn` для этого окна. Затем вешает на него window transformer, и Hyprland рендерит основной проход окна в отдельный буфер. Шейдер обрабатывает живое содержимое: окно отрисовывается и реагирует на ввод прямо во время анимации. Эффекты получают `uForOpening = true`, так что tv-glitch играет как «включение телевизора». Соседние окна раздвигаются штатной анимацией `windowsMove`.

## Эффекты

| Эффект | Что происходит | Длительность | Кадр, мс* |
|---|---|---|---|
| `tv-glitch` | помехи, затем окно схлопывается, как выключенный телевизор (порт BMW) | 750 мс | 2.7 |
| `incinerate` | окно загорается как бумага от случайной точки: подпалина, тлеющая кромка, затем пепел и искры улетают вверх | 1400 мс | 4.4 |
| `disintegrate` | окно рассыпается на крупинки самого себя, ветер уносит их вверх и назад | 1300 мс | 4.6 |
| `shatter` | от точки удара разбегается паутина трещин, осколки разлетаются и падают | 1100 мс | 5.6 |
| `slice` | взмах клинка: окно разрезано 1–3 линиями, куски разъезжаются и падают | 1000 мс | 4.3 |
| `black-hole` | окно закручивается в чёрную дыру с аккреционным кольцом | 900 мс | 0.9 |
| `hexagon` | волна шестиугольников сжимается от случайной точки, грани светятся | 900 мс | 0.8 |
| `energize` | луч транспортера: окно растворяется в мерцающем свете и искрах | 1100 мс | 1.6 |
| `doom` | оплавление экрана из Doom | 900 мс | 0.4 |
| `fade` | затухание с лёгким сжатием; запасной эффект | 300 мс | 0.3 |
| `debug-uv` | отладка ориентации | | |

\* Время кадра на Intel Iris Xe для окна 2560×1440 вместе с запасом под частицы. Для окна меньше время пропорционально площади, а часть запаса за краем экрана не рисуется. Бюджет кадра на 144 Гц — 6.9 мс.

При открытии эффект играет в обратную сторону: пепел собирается в окно, осколки слетаются, крупинки складываются. В `doom` колонки окна падают сверху.

Каждое закрытие получает свой случайный seed, поэтому точка возгорания, направление ветра, точка удара и линии разреза каждый раз другие.

## Установка

### Что нужно

- Hyprland 0.56 с рендерером OpenGL и заголовками той же версии, что запущена. На Arch заголовки ставятся вместе с пакетом `hyprland`.
- `cmake`, `ninja` (или `make`), `pkgconf`, компилятор C++23 (GCC 14+), `tomlplusplus`.
- Необязательно: `glslang` для офлайн-проверки шейдеров.

На Arch:

```sh
sudo pacman -S --needed base-devel cmake ninja pkgconf tomlplusplus glslang
```

### Скачать и собрать

```sh
git clone https://github.com/axenos-gr/hypr-window-anim.git
cd hypr-window-anim
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build                       # необязательно
cmake --install build --prefix ~/.local      # или sudo cmake --install build для /usr/local
```

Что ставится:
- `~/.local/bin/hypr-window-anim` — CLI;
- `~/.local/lib/hypr-window-anim/hypr-window-anim.so` и `.so.abi` — плагин и ABI, под который он собран;
- `~/.local/share/hypr-window-anim/` — конфиг по умолчанию и эффекты.

### Включить

Загрузить в текущую сессию, без перезапуска Hyprland:

```sh
~/.local/bin/hypr-window-anim load      # проверит ABI и загрузит плагин
hypr-window-anim status                 # hooks: ok — всё работает
hypr-window-anim preview incinerate     # посмотреть эффект: окно сгорит и вернётся
```

Чтобы плагин загружался при каждом входе, добавьте в `hyprland.conf`:

```
exec-once = ~/.local/bin/hypr-window-anim load
```

Полный путь нужен потому, что при старте Hyprland `~/.local/bin` может ещё не быть в `PATH`.

`load` сначала сравнивает ABI, под который собран плагин (файл `hypr-window-anim.so.abi` рядом с `.so`), с `abiHash` запущенного Hyprland. При несовпадении плагин не загружается. Сам плагин проверяет хеш ещё раз в `pluginInit`. Бинды менять не нужно: штатный `killactive` уже анимируется.

Конфиг необязателен, без него работают значения по умолчанию. Создать его для правки:

```sh
hypr-window-anim init        # ~/.config/hypr-window-anim/config.toml, применяется на лету
```

Выключить: `hypr-window-anim unload` и убрать строку `exec-once`.

### Через hyprpm (экспериментально)

Манифест `hyprpm.toml` есть, но этот путь пока не проверялся. Нужны те же зависимости:

```sh
hyprpm update
hyprpm add https://github.com/axenos-gr/hypr-window-anim
hyprpm enable hypr-window-anim
```

В `hyprland.conf` добавьте `exec-once = hyprpm reload -n`. hyprpm собирает только плагин, без CLI. Вместо CLI используйте `hyprctl hwanim status|reload|list|check|preview`.

### Обновление

У плагинов Hyprland нет стабильного ABI, поэтому после обновления Hyprland плагин нужно пересобрать. После обновления самого плагина — тоже:

```sh
cd hypr-window-anim
git pull
tools/check-hooks.sh                         # три перехватываемые функции на месте?
cmake --build build && cmake --install build --prefix ~/.local
hypr-window-anim restart                     # Hyprland держит в памяти старую сборку до перезагрузки
```

Установка безопасна и при загруженном плагине: `cmake --install` подменяет файл, а не переписывает его. Но новая сборка начнёт работать только после `restart`. Если на диске лежит сборка новее загруженной, `status` об этом предупредит.

## Конфиг

`~/.config/hypr-window-anim/config.toml` (или `$XDG_CONFIG_HOME/...`, или `$HWA_CONFIG_DIR`). Изменения применяются сразу.

```toml
[general]
enabled = true
default_effect = "tv-glitch"   # или список: ["incinerate", "shatter", "slice"]
hold_layout = true          # закрытия через WM ждут конца эффекта
app_close_effects = true    # анимировать и закрытия самим приложением
close_timeout_ms = 1500     # вернуть окно, если оно так и не закрылось
max_concurrent = 6
open_effects = true         # анимировать появление окон
open_effect = "tv-glitch"   # заменяет windowsIn/fadeIn Hyprland для анимируемых окон

[effects.tv-glitch]
duration_ms = 750
padding = 0                 # запас вокруг окна, логические пиксели

[effects.tv-glitch.params]  # strength -> uniform uStrength
color = "rgb(100, 160, 255)"
strength = 2.0

[[rules]]                   # первое совпадение выигрывает
class = "^(kitty)$"         # regex; ещё есть title, floating, xwayland
effect = "fade"
duration_ms = 400

[[rules]]
class = "^(rofi)$"
effect = "none"             # без анимации закрытия
open_effect = "none"        # и открытия
```

Правило с `hold = false` отключает ожидание для подходящих окон. Для открытия есть отдельные ключи правила: `open_effect` и `open_duration_ms`.

Вместо одного эффекта можно указать список. Это работает в `default_effect`, `open_effect`, а также в `effect` и `open_effect` правил. Для каждого окна берётся случайный эффект из списка, и один и тот же не выпадает два раза подряд. `"none"` в списке значит «иногда без анимации». `status` показывает списки как `random of …`.

Параметры эффектов по умолчанию лежат в `share/hypr-window-anim/effects/<имя>.toml`. В конфиге переопределяются только нужные:

```toml
[general]
default_effect = ["incinerate", "disintegrate", "shatter", "slice"]
open_effect = ["disintegrate", "hexagon"]

[effects.incinerate]
duration_ms = 1600

[effects.incinerate.params]
color = "rgb(90, 170, 255)"   # синее пламя
ash = 0.5                     # вдвое меньше пепла

[effects.slice.params]
cuts = 3

[[rules]]
class = "^(firefox)$"
effect = ["shatter", "black-hole"]
```

## Свои эффекты

Положите `~/.config/hypr-window-anim/effects/<имя>.frag`. Рядом можно положить `<имя>.toml` со значениями по умолчанию:

```toml
duration_ms = 800
padding = 0
[params]
color = "#ff8800"
```

Контракт совпадает с Burn-My-Windows, поэтому их шейдеры (`resources/shaders/*.frag`) работают почти без правок:
- `getInputColor(vec2)` / `setOutputColor(vec4)` — цвет окна и результат, straight alpha;
- `iTexCoord` — координаты 0..1 внутри окна, (0,0) — левый верхний угол;
- униформы: `uProgress` (0→1), `uDuration` (с), `uSize` (px), `uPadding` (px), `uForOpening`, `uIsFullscreen`, а также `uSeed`, `uTime`, `uMonitorScale`, если эффект их объявляет;
- хелперы BMW: `easeOutQuad`, `simplex2D`, `hash12` и другие из `shaders/bmw-common.glsl`.

Если эффект рисует за пределами окна (частицы, осколки), задайте `padding`. Тогда `iTexCoord` охватывает окно вместе с запасом, а окно занимает `[uPadding, uSize - uPadding]` в пикселях.

Шейдер собирается как GLSL ES 3.00. Ошибки ссылаются на строки файла эффекта (`1:<строка>`):

```sh
hypr-window-anim check            # через плагин, на реальном драйвере
hypr-window-anim check --offline  # glslangValidator, без Hyprland
hypr-window-anim preview fire     # проиграть эффект на активном окне, не закрывая его
```

Если эффект не компилируется, используется встроенный `fade`, а если не скомпилировался и он — штатная анимация Hyprland.

Для отладки без композитора есть офлайн-рендерер `hwa-sheet`. Он прогоняет шейдер на GPU через EGL без окна и ищет эффекты так же, как плагин:

```sh
cmake -B build -DHWA_BUILD_TOOLS=ON && cmake --build build
build/hwa-sheet myeffect --out sheet.png    # раскадровка в PNG (поверх шахматки, видна прозрачность)
build/hwa-sheet myeffect --open             # то же для открытия
build/hwa-sheet myeffect --image win.png    # на своём снимке окна
build/hwa-sheet myeffect --bench            # время кадра на окне 2560×1440
build/hwa-sheet myeffect --verify           # первый кадр совпадает с окном, последний пуст
```

`--verify` важен: если при открытии последний кадр не совпадает с окном, при снятии эффекта будет заметен скачок. С `-DHWA_BUILD_TOOLS=ON` `ctest` проверяет так все встроенные эффекты.

## CLI

| Команда | Что делает |
|---|---|
| `init [--force]` | создать конфиг |
| `load [--plugin PATH]` / `unload` / `restart` | загрузить с проверкой ABI / выгрузить / перезагрузить |
| `reload`, `status`, `list` | перечитать конфиг, состояние (с fps последних эффектов), список эффектов |
| `preview [effect]` | эффект на активном окне без закрытия |
| `check [--offline] [--all] [effect…]` | проверить конфиг и шейдеры |
| `doctor`, `instances`, `version` | диагностика |

`--instance SIG` направляет команду в конкретный экземпляр Hyprland. Dispatcher'ы для биндов: `hwanim:close [effect]` и `hwanim:preview [effect]`.

## Безопасное тестирование

Ошибка в плагине роняет весь композитор. Новые сборки проверяйте во вложенном Hyprland:

```sh
N=$(tools/nested-session.sh)         # окно на workspace 10, конфиг tools/nested.conf
build/hypr-window-anim -i "$N" load --plugin build/hypr-window-anim.so
hyprctl -i "$N" dispatch exec foot   # во вложенной сессии бинды на SUPER
```

`tools/testapps/close-behaviour.py dialog|ignore|slow` — тестовое окно, которое по-разному реагирует на запрос закрытия.

## Ограничения

- Только рендерер OpenGL.
- Не удерживаются (эффект при этом играет): fullscreen, окна в группах, закреплённые (pinned) окна, окна на рабочем столе в момент анимации переключения, закрытия жестом тачпада, окна с правилом `no_anim`.
- Если приложение закрывается медленно, после эффекта на его месте на мгновение остаётся пустое место. Если приложение закрывается дольше `close_timeout_ms`, окно успеет вернуться, и эффект сыграет второй раз.
- Открытие не анимируется для override-redirect окон X11 (меню, подсказки), окон на невидимом рабочем столе, окон за fullscreen-окном и окон с правилом `no_anim`. Им остаётся штатная анимация Hyprland.
- На повёрнутых мониторах эффект повёрнут вместе с буфером. HDR не учитывается.
- Если приложение снимает окно с экрана пустым буфером, снимок пуст, и эффекта не видно (так же, как у штатного fade-out).

## Лицензия

GPL-3.0-or-later. `effects/tv-glitch.frag` и `shaders/bmw-common.glsl` взяты из [Burn-My-Windows](https://github.com/Schneegans/Burn-My-Windows) (© Simon Schneegans, GPL-3.0-or-later).
