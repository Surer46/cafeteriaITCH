#ifndef APP_FRONTEND_UI_HELPERS_HPP
#define APP_FRONTEND_UI_HELPERS_HPP

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include <wx/wx.h>
#include <wx/listctrl.h>
#include <wx/odcombo.h>

// Utilidades de interfaz compartidas por los paneles: reparto de anchos de columnas en listas
// y campos de texto con el texto centrado verticalmente. Solo dependen de wxWidgets.
namespace app::frontend::ui {

// Ancho de un texto con la fuente REAL del encabezado de la lista (en Windows el encabezado
// nativo no hereda la fuente de la lista). Fuera de Windows usa la fuente de la lista.
int header_text_width(wxListCtrl* list, const wxString& text);

// Alto del encabezado de la lista.
int header_height(wxListCtrl* list);

// true si la lista muestra ahora la barra de desplazamiento vertical.
bool has_vertical_scrollbar(wxListCtrl* list);

// Descripción de una columna para distribute_list_columns.
struct ColumnSpec {
    wxString header;
    int content_px{0};         // ancho del contenido más largo en píxeles reales (GetTextExtent)
    int floor_content_px{-1};  // contenido que no puede recortarse en píxeles reales (-1: igual a content_px)
    int content_pad{16};       // relleno alrededor del contenido en unidades lógicas
    int extra_px{0};           // reserva adicional en unidades lógicas (p. ej. imagen de la fila en Touch)
};

// Reparte los anchos de columna de la lista:
//  - el encabezado (fuente real del encabezado + 16 px escalados) cabe completo en TODAS las columnas;
//  - el ancho útil excluye la barra vertical, tanto si ya está visible como si va a aparecer con las filas
//    actuales, de modo que nunca surge barra horizontal;
//  - la columna flexible toma todo el espacio restante (su contenido puede ir con puntos suspensivos);
//    si no alcanza para su encabezado, las demás ceden hasta su contenido protegido.
// Es idempotente: solo llama a SetColumnWidth cuando el ancho calculado difiere del actual.
void distribute_list_columns(wxListCtrl* list, const std::vector<ColumnSpec>& columns, std::size_t flexible_index);

// Filas de al menos 40 px en Touch: la altura de fila de SysListView32 sigue a la lista de imágenes.
void apply_touch_row_height(wxListCtrl* list);

// Fábrica de campos de texto.
//  - fixed_height == 0: devuelve el wxTextCtrl simple (también en `out`).
//  - fixed_height > 0: devuelve un contenedor con borde, fondo de superficie y espaciadores, con el
//    wxTextCtrl (sin borde) centrado verticalmente en su interior. Alto del contenedor =
//    max(fixed_height, mejor alto del campo + 2), así el resultado no depende del tema.
//    fixed_width < 0 deja el ancho libre. Un clic en el contenedor da foco al campo.
// Habilite/deshabilite siempre con set_text_field_enabled para que el contenedor siga al campo.
wxWindow* make_text_field(wxWindow* parent, wxTextCtrl*& out, int fixed_height, int fixed_width = -1,
                          const wxFont& font = wxNullFont, long style = 0);

// Habilita o deshabilita el campo; el fondo del contenedor (si lo hay) sigue al del campo.
void set_text_field_enabled(wxTextCtrl* edit, bool enabled);

// Lista desplegable de solo lectura con filas táctiles en perfil Touch.
class TouchComboBox : public wxOwnerDrawnComboBox {
public:
    TouchComboBox(wxWindow* parent, wxWindowID id = wxID_ANY,
                  const wxSize& size = wxDefaultSize,
                  const wxArrayString& choices = wxArrayString());

protected:
    wxCoord OnMeasureItem(size_t item) const override;
    void OnDrawItem(wxDC& dc, const wxRect& rect, int item, int flags) const override;
};

// Convierte una cantidad positiva entera en unidad de pantalla ("g", "kg", "ml", "l", "piezas")
// a la unidad base entera del contrato (mg, ml o pz). Valida desbordamiento.
bool parse_quantity_to_base_units(
    const std::string& raw_text,
    const std::string& unit_code,
    std::int64_t& out_base,
    std::string& out_error
);

} // namespace app::frontend::ui

#endif // APP_FRONTEND_UI_HELPERS_HPP
