// T17: textos de referencia de TicketDto::to_formatted_text generados con el formateador de ANTES de T17 (main tras B18).
// Con los campos de presentación en su valor por omisión el texto nuevo debe salir idéntico byte a byte.
#ifndef TESTS_TICKET_GOLDEN_T17_HPP
#define TESTS_TICKET_GOLDEN_T17_HPP

namespace ticket_golden {
inline const char* const k_card40 = R"GOLDEN(========================================
           Cafetería ITCH II            
========================================
Folio:   V-TEST-0001
Fecha:   2026-11-22 10:15:00
Cajero:  Cajero Prueba
----------------------------------------
Cant. Descripcion                Importe
----------------------------------------
2x Café americano grande          $60.00
  Desc. Promo de prueba larga     -$5.00
(2 x $30.00)
  Nota: sin azúcar
1x Pan                            $15.00
----------------------------------------
Subtotal:                         $75.00
Descuento:                         $5.00
Cargo por pago con tarjeta:        $2.80
TOTAL:                            $72.80
----------------------------------------
Metodo de Pago:                  TARJETA
========================================
      Gracias por su preferencia!       
========================================
)GOLDEN";
inline const char* const k_card28 = R"GOLDEN(============================
     Cafetería ITCH II      
============================
Folio:   V-TEST-0001
Fecha:   2026-11-22 10:15:00
Cajero:  Cajero Prueba
----------------------------
Cant. Descripcion    Importe
----------------------------
2x Café americano     $60.00
   grande
  Desc. Promo de pru. -$5.00
(2 x $30.00)
  Nota: sin azúcar
1x Pan                $15.00
----------------------------
Subtotal:             $75.00
Descuento:             $5.00
Cargo por pago con tarjeta:
                       $2.80
TOTAL:                $72.80
----------------------------
Metodo de Pago:      TARJETA
============================
Gracias por su preferencia! 
============================
)GOLDEN";
inline const char* const k_cash40 = R"GOLDEN(========================================
           Cafetería ITCH II            
========================================
Folio:   V-TEST-0001
Fecha:   2026-11-22 10:15:00
Cajero:  Cajero Prueba
----------------------------------------
Cant. Descripcion                Importe
----------------------------------------
2x Café americano grande          $60.00
  Desc. Promo de prueba larga     -$5.00
(2 x $30.00)
  Nota: sin azúcar
1x Pan                            $15.00
----------------------------------------
Subtotal:                         $75.00
Descuento:                         $5.00
TOTAL:                            $70.00
Reembolso V-TEST-0001-R1:        -$15.00
----------------------------------------
Metodo de Pago:                 EFECTIVO
Recibido:                        $100.00
Cambio:                           $30.00
========================================
      Gracias por su preferencia!       
========================================
)GOLDEN";
inline const char* const k_cash28 = R"GOLDEN(============================
     Cafetería ITCH II      
============================
Folio:   V-TEST-0001
Fecha:   2026-11-22 10:15:00
Cajero:  Cajero Prueba
----------------------------
Cant. Descripcion    Importe
----------------------------
2x Café americano     $60.00
   grande
  Desc. Promo de pru. -$5.00
(2 x $30.00)
  Nota: sin azúcar
1x Pan                $15.00
----------------------------
Subtotal:             $75.00
Descuento:             $5.00
TOTAL:                $70.00
Reembolso V-TEST-0001-R1:
                     -$15.00
----------------------------
Metodo de Pago:     EFECTIVO
Recibido:            $100.00
Cambio:               $30.00
============================
Gracias por su preferencia! 
============================
)GOLDEN";

} // namespace ticket_golden

#endif
