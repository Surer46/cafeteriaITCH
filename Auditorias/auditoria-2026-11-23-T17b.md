# Auditoría T17b — Cobro y reembolso en Táctil

Ronda 15 — T17b (Claude Code Terminal, rama `frontend/t17b`, `4e8e296`) — auditado el 2026-11-23

**T17b: aceptado.** Se integra en `main` después de T16e, para no cambiar el código mientras Neon compila y prueba. Entrega en `entregas/entrega-2026-11-23-T17b.md`.

- **Cobro Táctil (`checkout_dialog.*`):** con Tarjeta o Transferencia, la columna del teclado muestra una tarjeta «Pago con tarjeta» con el aviso de cobrar en la terminal de Mercado Pago; el diálogo ya no deja la mitad vacía y mide lo mismo con cualquier método (801×592 lógicos).
- **Reembolso (`refund_dialog.*`):** Táctil baja de 942×636 a 954×583 lógicos sin tocar mínimos (− y + siguen en 48); «Ya se preparó» pasa a la fila del producto y su ayuda a una línea («Marcada: queda como merma. Desmarcada: los ingredientes regresan al inventario.»).
- **Márgenes:** 16 lógicos dentro de las tarjetas de ambos diálogos.
- **Prueba (`test_t17_refunds_surcharge.py`):** solo endurece. El cobro mide igual con Tarjeta y Efectivo y cabe en 600 de alto; el reembolso cabe en 960x600; las capturas pueden ir a otra carpeta con `T17_QA_DIR`. Ninguna aserción se relaja.
- **Capturas revisadas:** `qa_visual/t17b/despues/release_tactil_cobro_tarjeta.png` y `release_tactil_reembolso.png`; correctas.
- **Verificación del ejecutor:** builds sin advertencias; CTest 23/23 en Release (dos veces) y Debug; en verde `test_t17`, `test_i2`, `test_t4` y `test_t2`. La verificación del orquestador se hace sobre `main` integrado con T16e.
- **Notas:** el cobro Táctil mide 592 de alto, sin holgura en 960x600. Una corrida falló una vez al abrir el menú «Más acciones» y pasó al repetirla sin cambios; si vuelve a pasar, se revisa.
