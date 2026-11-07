# Texturas servidas uno o dos niveles abajo (`texture_scale`)

> **Retirado el 6 nov 2026** al sincronizar con Jadyx `main` (`451b027`): el upstream resuelve lo
> mismo con `SetAstcDropTopLevel` (sesgo de niveles en el texture cache D3D12) y Permafrost. El
> código de esta opción queda en la historia (`becbe0a`); este documento se conserva como registro.

Candidato del 3 de noviembre de 2026. Implementa lo que el README deja como siguiente paso:
«An option to lower the resolution of the largest textures. It costs sharpness, but it would leave
enough room for the heaviest loads.» Esta revisión **no certifica** memoria ni FPS en la Series: el
gate de consola queda pendiente.

## Qué hace

Con `texture_scale=1` (o `2`) en `boot.cfg`, una textura muestreada grande se crea en el host
**empezando en el mip 1** (o 2): la mitad (o un cuarto) del borde, una cadena de mips una (o dos)
niveles más corta, y la vista SRV lleva `MostDetailedMip` desplazado lo mismo. El guest conserva su
copia completa en memoria emulada; no se remuestrea ni un texel.

Por qué es correcto sin tocar los shaders:

- Las coordenadas del guest son normalizadas: media resolución sigue cubriendo la misma superficie.
- `D3D12_SHADER_RESOURCE_VIEW_DESC.Texture2DArray.MostDetailedMip` define cuál es el nivel 0 de esa
  vista. Un nivel de guest `N` es el nivel de host `N - dropped`. El LOD por derivadas, el wrap y el
  filtrado los calcula el hardware sobre la cadena del host: no hay que sesgar samplers.
- El último nivel de host es el último del guest; D3D12 recorta el LOD a `MipLODClamp` de la vista,
  así que un LOD explícito fuera de rango cae en el mip más pequeño, no en memoria inválida.

El coste es nitidez: un mip (o dos) menos en las texturas más grandes, las que el juego ya muestra
con LOD alto la mayor parte del tiempo.

## Política (pura y probada)

`src/video_core/renderer_d3d12/d3d12_texture_downscale.h` decide, por textura:

| Entrada | Valor |
|---|---|
| Presupuesto por textura | 32 MiB (`ALLOWANCE_BYTES`) |
| Máximo de niveles | el de `texture_scale=` (0, 1 o 2) |
| Suelo | 64 px por eje (`MIN_EDGE`) |
| Excluye | MSAA, un solo nivel, imágenes convertidas/decodificadas (ASTC, BC arrays) |

`PlanFor()` devuelve `dropped_levels`, tamaño y bytes resultantes; `IsDropped()`, `HostLevel()` y
`GuestLevel()` traducen entre niveles. `tools/xbox/tests/texture-downscale.cpp` cubre huella por mip
(BC3 1024²=1 MiB, RGBA8 sin redondeo, capas), desactivación, MSAA, un solo nivel, el suelo, el
último nivel y el mapeo. Se compila y ejecuta sin device.

## Integración

1. `Image::Image`: si la opción está activa y la imagen es 2D (una capa), color, sin MSAA, con más
   de un mip y sin formato convertido, aplica el plan: `dropped_levels`, tamaño y `MipLevels` del
   `D3D12_RESOURCE_DESC`, y una línea de log por imagen (máximo 8).
2. `Image::Subresource`: suma `dropped_levels` al nivel y calcula el stride con los niveles de host.
3. `ImageView`: `base_level = range.base.level + dropped`, con el clamp que ya existía (una vista
   fuera del recurso sigue recortándose y avisando).
4. Subidas (`UploadMemoryImpl`): los niveles omitidos no se suben; el resto usa el mapeo de
   `Subresource`.
5. Descargas y GC: una imagen con niveles omitidos no descarga ni planifica GC (los niveles
   omitidos solo existen en memoria del guest, que manda). Se avisa una vez.
6. Copias y checkpoint: `CanCheckpointImage` la rechaza; las rutas de copia que la encuentren
   deben devolver sin copiar (ver límites).

## Límites conocidos (leer antes de activarlo)

- **Render targets.** Una imagen creada como muestreada puede usarse después como target por alias
  de direcciones del guest. Con la opción activa esa escritura iría al mip de host 0 (= mip 1 del
  guest). Es un fallo visual, no un cierre: las vistas siguen dentro del recurso. Si se activa en
  un juego que renderiza a texturas con mips, hay que comparar visualmente.
- **Un solo mip.** La mayoría de texturas de UI y muchas de luz no tienen cadena: no cambian.
- **Copias entre imágenes.** Una copia guest→guest que toque un nivel omitido no puede ejecutarse
  tal cual; el código que la encuentra debe saltarla. Si aparece corrupción, `texture_scale=0`.
- **Pulse.** Una imagen servida abajo no se captura (checkpoint la rechaza).

## Gates pendientes

1. **PC, tests puros:** `g++ -std=c++20 -I src tools/xbox/tests/texture-downscale.cpp` y ejecutar.
2. **Build UWP** (`uwp-x64`, target `jadyx`).
3. **PC con `texture_scale=1`**: el log debe mostrar `served 1 level(s) down` y `texture memory`
   por debajo del mismo punto con `texture_scale=0`; sin `RenderError`/`Critical`; comparar la
   misma zona visualmente.
4. **Series**: `memory_audit=1`, misma zona de la carga pesada que hoy devuelve a la biblioteca.
   Criterio de éxito: la carga termina sin OOM y el audit muestra menos MiB de texturas.
5. **A/B** `texture_scale=0/1` en la misma versión y zona; no atribuir FPS al cambio sin repetir.

## Fuentes

- `README.md` (muro de memoria, recorte de texturas, `texture_cap_mib`).
- `docs/xbox/xbox_texture_memory.md` (packing, GC, comparación con Vulkan).
- `src/video_core/renderer_d3d12/d3d12_texture_cache.cpp` (`Image::Image`, `Subresource`).
- `src/video_core/renderer_d3d12/d3d12_image_view.cpp` (clamp de vistas y `MostDetailedMip`).
- `docs/xbox/xbox_performance.md` (candidatos de memoria y su estado).
