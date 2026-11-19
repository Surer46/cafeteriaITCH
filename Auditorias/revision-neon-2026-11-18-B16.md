# Revisión a ciegas B16 — estado y último movimiento de productos para Existencias (I3)

Calibración (Neon, orquestador operativo en prueba) — auditado el 2026-11-18 sobre `<worktree del backend>`, rama `backend/b16`, commit `56a5603` (base `f9a5dc3`; integrado en `main` en `47fe690`). Sin leer `Auditorias/auditoria-2026-11-18-B16.md`.

**Veredicto: B16 aceptado con observaciones (menores, no bloqueantes).**

## 1. Alcance y propiedad de archivos

`git diff --ignore-cr-at-eol --stat f9a5dc3..56a5603` (8 archivos, +567 / −51 líneas):

- `include/app/contracts/dto.hpp` (+15)
- `include/app/storage/sqlite_database.hpp` (+4)
- `src/storage/sqlite_database.cpp` (+1)
- `src/services/sqlite_catalog_service.cpp` (+145 / −51)
- `src/services/mock_services.cpp` (+6)
- `tests/CMakeLists.txt` (+7)
- `tests/test_b16.cpp` (+388, nuevo)
- `backend/plan.md` (+1, solicitado explícitamente en `Prompts/prompts-2026-11-18-B16.md`)

Todos los archivos pertenecen al ámbito de backend; no se tocó `src/frontend/`, `tests/*.py` ni ninguna migración.

## 2. Revisión del código y las reglas

- **Contrato aditivo (`include/app/contracts/dto.hpp:86-100`):** añade `ProductDto::stock_status` y `ProductDto::last_movement_at` con comentarios en lenguaje llano que documentan los cuatro casos y la aclaración confirmada en la respuesta a la duda (combo con una parte sin control mantiene `stock_tracked = false` y ambos campos vacíos).
- **Lógica de cálculo (`src/services/sqlite_catalog_service.cpp:78-202`):**
  - Sin `double` ni `float`; solo aritmética entera (`std::int64_t`, `l.stock / l.qty` en `:83` y `ev.units / part.qty` en `:183`).
  - `eval_lines` (`:78-94`) y `status_of` (`:96`) coinciden con la definición de `IngredientDto::stock_status` en `src/services/sqlite_inventory_service.cpp:248-249,297-298`: `"OUT"` si `units <= 0`, `"LOW"` si algún ingrediente tiene `stock > 0 && threshold > 0 && stock <= threshold`, si no `"OK"`. Umbral 0 nunca produce `"LOW"`.
  - `fill_stock_bulk` (`:104-196`) y `fill_stock_info` (`:198-202`) se usan en las tres funciones de lectura pedidas: `search_products` (`:315`), `get_product_by_id` (`:356`) y `get_product_by_sku_or_barcode` (`:398`). Permisos intactos (`search_products` sigue exigiendo sesión y `inventory.view`/`pos.sell`/`pos.visual_catalog` en `:240-252`; las otras dos exigen sesión en `:321-323` y `:362-364`).
  - `SqlitePromotionService::get_combo` (`src/services/sqlite_promotion_service.cpp:659-660`) ya delega `ComboDto::stock_units` en `catalog_service_->get_product_by_id`, así que sigue consistente.
  - **Sin N+1 y sin migración:** `fill_stock_bulk` agrupa por tandas de `kChunk = 400` (`:105`) una consulta de `combo_items` (`:125-126`) y otra de recetas vigentes + ingredientes + `MAX(stock_movements.created_at)` (`:144-148`). Los índices necesarios ya existían: `idx_stock_movements_ingredient` sobre `stock_movements(ingredient_id, created_at)` (migración `011`), `idx_recipes_product` sobre `recipes(product_id)` (`003`), `idx_recipe_items_recipe` sobre `recipe_items(recipe_id)` (`003`) y `PRIMARY KEY (combo_product_id, component_product_id)` en `combo_items` (`019`). No hacía falta migración 021.
- **Mock (`src/services/mock_services.cpp:394-399`):** el constructor de `MockCatalogService` asigna `stock_tracked`, `stock_status` y `last_movement_at` a los cinco productos simulados; las tres funciones de lectura del mock devuelven ese vector.
- **Pruebas nuevas (`tests/test_b16.cpp:1-388`, `tests/CMakeLists.txt:251-256`):**
  - Usa `TEST_CHECK` (activo en Debug y Release), migraciones por `SOURCE_MIGRATIONS_DIR`, directorio temporal aislado que limpia al final y no escribe ningún `.exe`.
  - Cubre directo (`OUT`/`OK`/`LOW`, umbral 0, igualdad con `get_ingredient`), preparado (`OUT` por falta de un ingrediente o por no alcanzar una porción, `LOW` por cualquiera de dos ingredientes, `last_movement_at` más reciente), combo (`OUT`/`LOW`/`OK`, `last_movement_at`), combo con parte sin control (`!stock_tracked`, campos vacíos, `stock_units` solo de partes controladas), directo sin control, preparado sin receta, actualización de `last_movement_at` tras compra/merma/venta y formato de 19 caracteres, mock, igualdad entre las tres lecturas y rendimiento con 500 productos (450 preparados + 50 combos).

## 3. Cambios no previstos en el plan y observaciones menores

1. **Contador de consultas en clase de producción (cambio fuera de plan, inofensivo):** `include/app/storage/sqlite_database.hpp:76-78,120` y `src/storage/sqlite_database.cpp:493` añaden `SqliteDatabase::prepared_statement_count()` sin guarda `CAFETERIA_TEST_HOOKS`, con el comentario «solo para pruebas de rendimiento». Solo incrementa un `std::uint64_t` en `prepare()` (no cuenta `execute()`), sin alterar comportamiento, datos ni seguridad; conviene dejarlo registrado porque la regla general mantiene utilidades de prueba fuera de producción.
2. **Medición de rendimiento con `stock_movements` vacía:** `tests/test_b16.cpp:314-372` siembra 500 productos, 40 ingredientes, 900 líneas de receta y 150 partes de combo, pero **0 filas en `stock_movements`**. Por tanto los 2–3 ms medidos confirman la ausencia de N+1 (4 consultas con 500 productos frente a 2 con 9), pero no miden la subconsulta `SELECT MAX(m.created_at) FROM stock_movements` con miles de movimientos acumulados (mitigado por el índice `idx_stock_movements_ingredient`).
3. **`create_product` no rellena los campos nuevos (`src/services/sqlite_catalog_service.cpp:515-528`):** al crear un producto directo con `track_stock = true` (o `link_ingredient_id`), el `ProductDto` retornado lleva `stock_tracked = true` pero `stock_status` y `last_movement_at` vacíos (igual que `stock_units = 0`, heredado de B14). Cumple el prompt (que pidió las tres funciones de lectura), pero si alguien usa el DTO devuelto por `create_product` sin releer verá estado vacío.
4. **Detalles menores en pruebas, comentarios y mock:**
   - `tests/test_b16.cpp:202,228`: declara y crea el ingrediente `c` («Sal B16») y luego lo descarta con `(void)c;`.
   - `tests/test_b16.cpp:232-277`: en combos no ejercita que la parte *preparada* entre en `LOW` ni el caso `0 < ev.units < part.qty` (`stock_units == 0` → `OUT`), aunque el código en `sqlite_catalog_service.cpp:183-192` sí lo cubre.
   - `src/services/sqlite_catalog_service.cpp:49` dice «número FIJO de consultas, sin importar cuántos productos sean» cuando en realidad escala en tandas de `kChunk = 400` (`1 + ceil(combos/400) + ceil(recetas/400)`, sin N+1); en `:61-62` el comentario interno no aclara que el estado del combo requiere `all_tracked` (el código en `:191` y `dto.hpp:92-94` sí son precisos).
   - `src/services/sqlite_catalog_service.cpp:127,149`: ante fallo de `prepare()` en una tanda, `fill_stock_bulk` retorna en silencio dejando productos con `stock_tracked = false` (mismo patrón que tenía `fill_stock_info`).
   - `src/services/mock_services.cpp:395-399`: los cinco productos simulados tienen `stock_units` entre 15 y 100 (> 10), de modo que todos quedan en `"OK"` (ninguno muestra `"LOW"` ni `"OUT"`).
   - `backend/plan.md:124` se marcó `[x]` antes de auditoría (práctica repetida en bloques de backend anteriores; el commit sí indica «sin auditar»).

## 4. Verificación ejecutada en `<worktree del backend>` (`backend/b16`)

Con `<toolchain>\mingw64\bin` y `<toolchain>\wxwidgets\lib\gcc1610_x64_dll` al inicio del `PATH`. Antes de cada corrida de CTest se comprobó con `tasklist` que no hubiera `app_desktop.exe` ni `ctest.exe` en ejecución.

| Paso / comando | Código de salida | Resultado observado |
|---|---|---|
| `git status -sb`, `git log -n 5`, `git diff --ignore-cr-at-eol --stat f9a5dc3..56a5603` | 0 | Rama `backend/b16` limpia en `56a5603`; 8 archivos. |
| `tasklist` + `Test-Path <toolchain>\mingw64\bin\ctest.exe` + `(Get-MpThreatDetection).Count` (antes) | 0 | Sin `app_desktop.exe` ni `ctest.exe`; `ctest.exe` presente (`True`); Defender = **37**. |
| `cmake --build <worktree del backend>\build_be_release --parallel 2` | 0 | Al día (solo copia de recursos y migraciones; 0 archivos C++ recompilados). |
| `cmake --build <worktree del backend>\build_be_debug --parallel 2` | 0 | Al día (solo copia de recursos y migraciones; 0 archivos C++ recompilados). |
| `ctest --test-dir <worktree del backend>\build_be_release --output-on-failure` | 0 | **21/21 aprobadas** (147.87 s; `ProductStockStatusTest` 0.30 s). |
| `ctest --test-dir <worktree del backend>\build_be_release -R ProductStockStatusTest -V` | 0 | **1/1 aprobada** (0.41 s). `[MEDIDA] search_products con 500 productos: 2 ms y 4 consultas (con 9 productos: 2 consultas)`. |
| `ctest --test-dir <worktree del backend>\build_be_debug -R ProductStockStatusTest -V` | 0 | **1/1 aprobada** (1.01 s). `[MEDIDA] search_products con 500 productos: 7 ms y 4 consultas (con 9 productos: 2 consultas)`. |
| `ctest --test-dir <worktree del backend>\build_be_debug --output-on-failure` | 0 | **18/21 aprobadas + 3 omitidas** esperadas en Debug (`PackageTest`, `InstallerTest`, `EndSessionTest`; 202.25 s; `ProductStockStatusTest` 0.82 s). |
| `(Get-MpThreatDetection).Count` + `Test-Path ctest.exe` + `git status -sb` (después) | 0 | Defender = **37** (0 detecciones nuevas); `ctest.exe` presente (`True`); worktree limpio en `backend/b16`. |

## 5. Lo que no se verificó y límites de esta calibración

- **Pruebas GUI y verificación en `<copia principal>` (`main`):** fuera del alcance de esta calibración (no se compiló `build_release`, `build_debug` ni `build_hooks_release`, ni se ejecutó la batería GUI, porque Gemini puede estar trabajando T14 en `main`).
- **Segunda corrida completa de CTest en Release:** se ejecutó una corrida completa de CTest en Release (21/21) más `ProductStockStatusTest -V` aparte, y una corrida completa en Debug (18 aprobadas + 3 omitidas); no se repitió la corrida completa de Release por segunda vez.
- **Advertencias de compilación desde cero:** `build_be_release` y `build_be_debug` ya tenían los binarios al día (posteriores a la última edición de `sqlite_catalog_service.cpp`); no se limpió la carpeta de compilación para no alterar `<worktree del backend>`, por lo que no se re-verificó el conteo de advertencias del compilador en frío.
- **Transparencia sobre la lectura a ciegas:** no se leyó `Auditorias/auditoria-2026-11-18-B16.md`. Al buscar con `Select-String` las líneas de `docs/ORQUESTADOR.md` para omitir la entrada de B16, la línea 50 apareció en la salida de búsqueda; toda la auditoría anterior se realizó desde el diff, el código y las ejecuciones propias en `<worktree del backend>`.

## Revisión del supervisor (Claude, 2026-11-18)

**Aprobada.** Comprobé en `backend/b16` (`56a5603`) las afirmaciones que sostienen las observaciones, y todas son correctas:

- `create_product` (`src/services/sqlite_catalog_service.cpp:515-528`) devuelve `stock_tracked` pero deja `stock_status` y `last_movement_at` vacíos.
- La prueba de rendimiento (`tests/test_b16.cpp:314-372`) no siembra ninguna fila en `stock_movements`.
- El comentario de la línea 49 dice «número FIJO de consultas», pero el código parte en tandas de `kChunk = 400` (líneas 105, 123 y 141).
- `(void)c;` en `tests/test_b16.cpp:228`.
- Los cinco productos del mock tienen existencia de 15 a 100, así que todos quedan en «OK».

**Comparación con la auditoría del supervisor** (`auditoria-2026-11-18-B16.md`):
- **Coincide:** veredicto, contrato, reglas de estado, combo con una parte sin control, ausencia de N+1 sin migración, contador `prepared_statement_count` fuera de plan.
- **Hallazgos de Neon que el supervisor no vio:** `create_product` sin los campos nuevos; medición de rendimiento sin movimientos; comentario «FIJO» inexacto; mock sin casos LOW ni OUT; huecos de prueba en combos; `backend/plan.md` marcado `[x]` antes de auditar.
- **Lo que Neon no podía ver:** el fallo intermitente de `PackageTest` apareció en la verificación en `main`, que quedó fuera de la calibración por instrucción.
- **Reglas:** respetadas (sin commits, sin compilar en `main`, `tasklist` antes de CTest, Defender antes y después, transparencia sobre la línea de `ORQUESTADOR.md` que vio al buscar).

**Seguimiento:** las observaciones 3.2 (rendimiento con movimientos) y 3.3 (`create_product`) van a la cola del backend como un bloque corto; ninguna bloquea T14.
