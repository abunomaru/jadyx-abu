# Alcance: port nativo de Pokopia (pokopia-recomp) a UWP

Revisión del 3 de noviembre de 2026 contra `drakolordx7/pokopia-recomp` (commit `2c29515`,
2 nov 2026), actualizada el 5 de noviembre contra la **v0.2.0** (4 nov 2026, commit `5339bf8`).
Ver [Cambios hasta la v0.2.0](#cambios-hasta-la-v020-5-nov-2026). **Documento de alcance, no de implementación.** No hay código del recomp en este repo;
las herramientas del gate P0 están en [`tools/xbox/pokopia/`](../../tools/xbox/pokopia/README.md).

## Qué es pokopia-recomp

- Recompilación estática de Pokémon Pokopia (Switch 2, **versión 2.0.1a**): el código ARM64 del
  juego se traduce a C++ con nxrecomp y se compila para x64 Windows con clang-cl. GPL-3.0.
- Requisitos publicados: Windows 10/11 x64, GPU con **Vulkan 1.3**, **16 GB de RAM**.
- El launcher (Tauri) valida un volcado descifrado (`base/{exefs,romfs}` y
  `updates/<versión>/{exefs,romfs}`), compila el juego (~7 min) y construye la caché de shaders
  (1–2 h). No incluye archivos del juego.
- Tamaño del runtime: ~95,6k líneas; gráficos ~44,7k, de las que ~18,5k están en ficheros que
  llaman a Vulkan.

Fuentes: https://github.com/drakolordx7/pokopia-recomp ·
https://x.com/PixelCNinja/status/2117351716307796367

## Por qué encaja mejor que un emulador

- **Sin JIT.** El juego es código compilado dentro del ejecutable: no hay generación de código en
  tiempo de ejecución ni el problema W^X del AppContainer.
- **Sin emulación de CPU.** Desaparece el muro de CPU de NX2 (MKW 15–23 FPS en la Deck, limitado
  por el JIT; ver [`xbox_nx2_scope.md`](xbox_nx2_scope.md)).
- **Shaders precompilados a SPIR-V** (Maxwell/Ampere → SPIR-V con el traductor de Ryujinx, validado
  con `spirv-val`). Jadyx ya convierte SPIR-V en DXIL con `spirv_to_dxil`. La caché puede hacerse
  en PC y copiarse a la consola.
- **Memoria identidad en direcciones fijas** (`runtime/include/nxrt/memory.h`): main en
  `0x7100000000`, SDK en `0x7500000000`, heap en `0x7800000000` con 12 GiB de reserva. La sonda de
  la 0.2.51.0 ya probó en la Series reservar 512 GiB, el aliasing de vistas y
  `VirtualProtectFromApp` con fallo capturado ([`xbox_d3d12_phase4.md`](xbox_d3d12_phase4.md)).

## Qué hay que cambiar

| Área | Hoy en el recomp | Para UWP en la Series |
|---|---|---|
| Gráficos | Solo Vulkan 1.3: `dynamicRendering`, `synchronization2`, `timelineSemaphore`, `descriptorIndexing`, `VK_EXT_transform_feedback` | Backend D3D12 FL 11.0 sin Agility SDK para la capa NVN. **Trabajo de meses.** Dozen (Vulkan sobre D3D12 de Mesa) no está verificado en FL 11.0 para Vulkan 1.3 |
| Heap | Sección de 12 GiB respaldada por el pagefile (`runtime/src/mem/memory.cpp:462` en la v0.2.0, `CreateFileMappingW(INVALID_HANDLE_VALUE, SEC_RESERVE)`) | La Series limita toda la memoria de sección respaldada por el pagefile a ~1,1 GiB (sonda de commit). Pasar a memoria privada o a una sección sobre archivo sparse, como Jadyx 0.2.56.0 |
| Memoria | `VirtualAlloc`, `VirtualProtect`, `MapViewOfFileEx` a direcciones fijas; write-watch con `VirtualProtect` + VEH | Variantes `*FromApp` (ya en `src/common/host_memory.cpp`); VEH ya en uso |
| Plataforma | `CreateWindowExW`, `CreateFileW`, `LoadLibrary*`, `CreateProcessW` | CoreWindow, acceso de almacenamiento UWP, `LoadPackagedLibrary`; la compilación del juego se queda en el PC y solo va al appx el resultado |
| Entrada/audio | XInput, XAudio2 | XAudio2 sirve tal cual (Jadyx ya lo usa); mejor Windows.Gaming.Input para mandos |

## El muro de memoria

- El recomp pide 16 GB y el juego reserva un heap de ~9,5 GiB (issue #16 de Eglantine). Es espacio
  de direcciones; nadie ha publicado el consumo real.
- Un juego Dev Mode tiene **5120 MiB** con la GPU incluida y sin pagefile.
- El recomp no tiene JIT, caché de bloques ni CPU emulada, así que su sobrecoste es menor que el de
  un emulador: es el mejor candidato posible, pero la cifra decide.

## Plan por fases

| Fase | Trabajo | Gate |
|---|---|---|
| P0 | `tools/xbox/pokopia/pokopia.ps1` en el PC: volcado, setup del launcher, corrida A sin límite y B con 5120 MiB | Estimación (pico de working set + GPU dedicada) ≤ 5120 MiB; si no, se para aquí |
| P1 | Heap en memoria privada o sección sobre archivo; `*FromApp` en `mem/` y `kernel/svc_memory.cpp` | Arranque en PC con commit de sección < 1 GiB |
| P2 | Backend D3D12 de la capa NVN; SPIR-V → DXIL con `spirv_to_dxil` y caché hecha en PC | Gameplay en PC con D3D12, sin Vulkan |
| P3 | Capa de plataforma UWP (ventana, archivos, carga de DLL, mandos) | Arranque en AppContainer en PC con `local-run.ps1` |
| P4 | Series con cap 5120 y `memory_audit=1` | Sesión larga sin OOM; FPS medido |

Estimación honesta: P1 y P3 son semanas; **P2 son meses**. Sin la cifra de P0 no tiene sentido
empezar P2.

Atajos posibles para P2, **sin verificar en FL 11.0**: una capa Vulkan→D3D12 dentro del proceso,
como la experimental de Cemu-UWP-Host (que dice usar Dozen solo como referencia de diseño, no como
dependencia), o el fork de Dozen para UWP/Xbox `Cosmo/mesa_ai_slop_version` (escrito por un LLM,
sin estado ni pruebas publicadas). Probarlos al empezar P2 antes de escribir un backend NVN→D3D12
desde cero.

## Cambios hasta la v0.2.0 (5 nov 2026)

- **v0.2.0 (4 nov), «third test build».** Los builds de la 0.1.1 no valen con el launcher nuevo y la
  caché de shaders cambió: hay que repetir *Set up*. ~15 correcciones de shaders (memoria
  compartida, atómicos, lecturas de textura) verificadas en una RTX 3080; los 99 248 shaders de la
  caché pasan validación. Primeras llamadas a DLSS nativo y más funciones de la GPU de Switch 2.
- **Los temporizadores del juego duraban ~40 % menos que en la consola**; la v0.2.0 lo corrige. Una
  medición P0 hecha con la 0.1.x jugó otro ritmo: el gate solo cuenta con la v0.2.0 o posterior.
- **El launcher acepta un NSP/XCI más los ficheros de claves**, además de carpetas extraídas
  (sin probar con un volcado real, según la propia nota). Las herramientas de este repo siguen sin
  descifrar nada: `prepare-dump.ps1` solo enlaza carpetas ya extraídas.
- **El heap no cambió:** sigue siendo una sección de 12 GiB respaldada por el pagefile (ahora en
  `memory.cpp:462`). P1 sigue vigente.
- **Medidor propio:** el runtime trae un muestreador (`runtime/src/diag/gauge.cpp`) que escribe una
  línea JSON por muestra con los contadores del proceso y todos los *gauges* registrados (texturas
  vivas, pools, cachés). Se activa con `NXRT_DIAG_SOAK_CSV=<ruta>` (o `[diag] soak_csv`).
  `memory-gate.ps1` lo activa y resume el pico de cada campo, para que un «no cabe» diga qué ocupa.
- **Repositorio nuevo del autor:** `drakolordx7/switch2-recomp` («Switch 2 games recompiled to run
  natively on PC»), con Pokopia como único juego por ahora en `recomp/games/pkp/` y
  `runtime/games/pkp/`. Sigue siendo solo Vulkan 1.3 y 16 GB; no menciona Xbox. Si el runtime pasa
  a ser común a varios juegos, el trabajo de P1–P3 serviría para todos.
- **Sin datos de RAM publicados todavía:** ni GitHub ni la prensa recogen consumo medido. El fork
  `featureJosh/pokopia-recomp` (PR #5) limita los trabajos de compilación a RAM libre / 2 GiB porque
  el *Set up* agotaba memoria en portátiles; afecta al PC, no a la consola.

## Forks de la comunidad y lo que sirve al emulador (6 nov 2026)

**Primera cifra de memoria.** En una Steam Deck LCD (issue #46 de `featureJosh/pokopia-recomp`, con
un build anterior a la v0.2.0), los pools de memoria GPU importados suman 3500 + 332 + 500 + 400 MiB
= **4732 MiB (~4,6 GiB)**, solo la parte gráfica y sin contar la memoria de CPU del juego. Un port
nativo sin sobrecoste de emulación ya roza los 5120 MiB del modo Game. Para el gate P0 esto apunta a
«no cabe» si no se reduce el pool grande (3500 MiB) o se manda parte a almacenamiento (Permafrost).

**Rendimiento en Deck.** Unos 26–54 FPS con Proton; el límite es el envío de trabajo a la GPU desde
la CPU (~28 ms por frame en #46), no la GPU, que queda ociosa. `featureJosh` prepara una revisión de
rendimiento (issue #55). El fork Linux `tinfoil-chewer/pokopia-recomp-linux` declara ~54 FPS en una
Deck LCD.

**Correcciones del recomp contrastadas con este fork.** Todas estaban ya cubiertas:

| Fallo en el recomp | Estado en este fork |
|---|---|
| Esperas y temporizadores ~1,63× cortos (contador a 19,2 MHz en lugar de 31,25 MHz) | Cubierto: `core.cpp:386` fija `Switch2CNTFRQ` al arrancar un título de Switch 2; el reloj, dynarmic, NCE y `psc/time` leen `GetGuestCNTFRQ()`. `NSToCNTPCT` (19,2 MHz fijos) no tiene usuarios |
| Seno y coseno desfasados 2π (niebla, agua, viento) | Cubierto: en SM70+ `MUFU.SIN/COS` toman vueltas; `lower_to_maxwell.h:2541` multiplica por 2π (`0x40c90fdb`) antes del `MUFU` de Maxwell |
| Salidas del vertex shader sin escribir a NaN en AMD (cielo y nubes negros) | Cubierto: el prólogo del vertex stage de SPIR-V escribe (0, 0, 0, 1). Importa porque la GPU de la Series es AMD (RDNA 2) |
| Copia GPU de la memoria del juego en AMD en lugar de `VK_EXT_external_memory_host` | No aplica: el backend D3D12 sube la memoria guest por su propio camino |

**Ideas aprovechables más adelante.**

- Los shaders de arranque empaquetados en un solo archivo zstd bajaron la espera de arranque en
  microSD de ~60 s a ~10 s. Aplicable a la caché de pipelines en el almacenamiento externo de la
  Series.
- Corrección del swizzle de half-float (sombras del sol y las nubes) en su traductor de shaders.
  Ese traductor viene de Ryujinx, no del recompilador de Eden: no se porta tal cual, pero conviene
  revisar los swizzles `HADD2/HMUL2/HFMA2` de la ruta nativa si aparecen sombras rotas.
- La v0.1.1 copia a la CPU los resultados de la GPU solo cuando el juego los lee, como el
  `GpuWrittenPages` que ya trae Jadyx.

## Reglas

- **Nada del juego en el repo ni en la nube:** ni el volcado, ni `exefs`, ni el C++ generado, ni el
  `pkp.exe` o el appx resultante. Todo se queda en el PC del usuario y en su consola.
- Este repo no descifra contenedores (`.nup`, `.nsp`, `.xci`, `.nca`): el usuario extrae su copia
  con sus herramientas y sus claves; `prepare-dump.ps1` solo enlaza carpetas ya extraídas.
- El appx generado contiene código derivado del juego: es para la consola del usuario y no se
  publica.
- GPL-3.0 y atribución a nxrecomp, Ryujinx, yuzu, Eden y el resto de créditos del recomp.

## Fuentes

- `pokopia-recomp` `README.md`, `launcher/src-tauri/src/{dump,gamebuild,play,shaders}.rs`,
  `runtime/src/mem/memory.cpp`, `runtime/include/nxrt/memory.h`, `tools/shaders/README.md`.
- Jadyx: [`xbox_d3d12_phase4.md`](xbox_d3d12_phase4.md) (sondas de fastmem y commit),
  `src/common/host_memory.cpp`, [`xbox_nx2_scope.md`](xbox_nx2_scope.md).
- https://github.com/Djihads80/Eglantine/issues/16 (heap de ~9,5 GiB).
- https://github.com/drakolordx7/pokopia-recomp/releases/tag/v0.2.0 ·
  https://github.com/drakolordx7/switch2-recomp ·
  https://github.com/featureJosh/pokopia-recomp/pull/5 ·
  https://github.com/featureJosh/pokopia-recomp/issues/46 ·
  https://github.com/featureJosh/pokopia-recomp/issues/55 ·
  https://github.com/tinfoil-chewer/pokopia-recomp-linux
- https://github.com/rodrigoandrigo/Cemu-UWP-Host · https://github.com/Cosmo/mesa_ai_slop_version
