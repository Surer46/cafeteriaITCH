# Auditoría general de alcance — 2026-11-12

Pregunta: ¿la aplicación planificada está completa?, ¿qué convendría agregar sabiendo que en el futuro se quiere una versión móvil?

Base revisada: `docs/specs.md` secciones 1 a 11, `backend/plan.md`, migración 001 y los generadores de identificadores, fechas y folio en `src/services/`. No se revisó `frontend/plan.md` línea por línea ni se comprobó si las casillas sin marcar de `backend/plan.md` siguen vigentes. Nada de esto se ejecutó: es lectura de documentos y código.

## Veredicto

Para **una sola caja en una PC**, la especificación cubre lo esencial: venta, cobro, ticket, turnos, inventario con recetas y costos, promociones, reportes, bitácora, cuentas, roles y respaldos. No falta ningún módulo grande. Faltan siete piezas menores (apartado A) y cuatro ajustes baratos de datos que hoy cuestan poco y después de acumular ventas cuestan mucho (apartado B).

La versión móvil **no cabe en la arquitectura actual tal como está escrita**: la especificación prohíbe servidor local, segunda base activa y sincronización. No hay que construir nada de eso ahora, pero sí evitar decisiones que lo cierren.

## A. Lo que falta para operar una cafetería

| # | Falta | Estado en la especificación | Recomendación |
|---|---|---|---|
| A1 | **Recuperación de la cuenta Dueño** si olvida su contraseña | No existe. Sin cuentas predeterminadas, un olvido deja el negocio sin administrador | Definir un procedimiento sin puerta trasera: código de recuperación generado en el alta inicial, que el Dueño imprime o anota. **Prioridad alta** |
| A2 | **Comanda de preparación** (cocina o barra) y número de pedido para el cliente | No existe. Las notas («sin azúcar») solo salen en el ticket del cliente | Segundo formato de impresión por venta y estado del pedido (pendiente, listo, entregado). Es además el puente natural hacia pedidos desde móvil |
| A3 | **Anulación y devolución** de venta cobrada | Descrita, con permiso `pos.void_sale`, sin servicio ni tablas | Ya prevista; depende de la sección 9. Diseñarla antes de I4 porque afecta el corte |
| A4 | **Importación del catálogo inicial** | Una mención en la sección 8, sin tarea ni contrato | Importar productos e ingredientes desde CSV con validación y vista previa. Sin esto, el piloto arranca capturando todo a mano |
| A5 | **Exportación de reportes** y corte imprimible | «Según formato confirmado», sin formato | Fijar CSV como mínimo; PDF o impresión térmica del corte |
| A6 | **Cajón de dinero** | Solo como pregunta en la sección 9 | Si la impresora lo abre por pulso, es un comando al imprimir; decidir con el hardware real |
| A7 | **Actualización de la aplicación** en la caja | Una frase en la sección 11 | Definir cómo llega una versión nueva y garantizar respaldo automático antes de migrar (casilla abierta en BE-01) |

Ya planificado y sin hacer, que conviene no perder de vista: bloqueo por inactividad y límite de intentos de acceso; registro de errores en archivo; comprobación de permisos en todos los servicios (BE-02 la marca incompleta en catálogo, inventario, carrito y venta: **verificar si sigue vigente**).

Preguntas de negocio que la especificación no hace y podrían cambiar el alcance: ¿se requiere factura (CFDI) o desglose de IVA?; ¿hay ventas a crédito, vales o cuentas de alumnos y personal?

## B. Preparar el terreno para móvil sin construirlo

Primero hay que decidir qué significa «versión móvil», porque cada opción cuesta distinto:

1. **Consulta para el Dueño** (ventas del día, alertas de inventario desde el teléfono). La más barata: solo lectura.
2. **Segundo punto de captura** (tableta o teléfono que toma pedidos y los manda a la caja). Requiere que la caja atienda a otros dispositivos.
3. **Pedidos de clientes** (alumnos piden desde su teléfono). Requiere servidor en internet, cuentas de cliente y probablemente pagos.

**Estado al 2026-11-12:** el cliente aún no ha especificado el tipo de versión móvil. Los cuatro ajustes de abajo sirven para las tres opciones, así que no dependen de esa respuesta.

### Lo que ya juega a favor

- La regla de capas: dominio y servicios no dependen de wxWidgets. Ese C++ puede compilarse para Android o iOS, o ponerse detrás de un servicio. La interfaz móvil se reescribe en cualquier caso: wxWidgets no tiene versión móvil.
- El inventario y la caja se guardan como movimientos, no como saldos editados. Es el modelo que mejor tolera varios dispositivos.
- Dinero en centavos enteros y DTO planos, fáciles de transmitir.

### Cuatro ajustes baratos ahora y caros después

| # | Hallazgo | Riesgo | Ajuste |
|---|---|---|---|
| B1 | Los identificadores son `prefijo-milisegundos-contador` (`generate_id` en cada servicio) | Dos dispositivos pueden generar el mismo identificador; el contador reinicia en cada arranque | Pasar a UUID aleatorio, o incluir un identificador de dispositivo. Una sola función compartida en lugar de una copia por servicio |
| B2 | Las fechas se guardan en hora local sin zona (`YYYY-MM-DD HH:MM:SS`). La sección 6 de la especificación pide conservar la zona | Imposible ordenar con certeza eventos de dos dispositivos o tras un cambio de horario | Guardar UTC, o añadir el desplazamiento. Decidirlo antes de I3; hoy contradice a la propia especificación |
| B3 | Productos, ingredientes y recetas solo tienen `created_at` | Sin «última modificación» no se puede enviar a otro dispositivo solo lo que cambió | Añadir `updated_at` por migración a las tablas de catálogo |
| B4 | El folio es una secuencia local única | Dos cajas generarían el mismo folio | Incluir el código de la caja en el folio |

### Decisión de diseño para los contratos nuevos (I3 en adelante)

La sesión vive en memoria dentro del proceso: los servicios preguntan «quién es el usuario actual». Con varios dispositivos, cada petición debe decir quién la hace. No conviene rehacer lo existente, pero sí que los contratos nuevos reciban el actor de forma explícita cuando sea razonable.

### Lo que no recomiendo hacer ahora

Servidor HTTP, sincronización, cuentas de cliente o pagos en línea. Contradicen `AGENTS.md` y la sección 6, y retrasarían el piloto. La vía móvil más barata, llegado el momento, es la opción 1 alimentada desde el servidor de respaldos, que hoy solo guarda copias; ampliarle el papel exige cambiar la especificación de forma explícita.

## Orden sugerido

1. Ahora, en backend, antes de I3: B1, B2, B3 y B4 en un solo bloque con migración (pocos datos reales todavía).
2. Con las decisiones de la sección 9: A1 (recuperación del Dueño) y A3 (anulaciones).
3. Antes del piloto: A4 (importar catálogo) y A7 (actualización con respaldo previo).
4. Después del piloto: A2 (comandas), A5, A6 y la decisión sobre el tipo de versión móvil.
