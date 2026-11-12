#ifndef APP_FRONTEND_UI_HELPERS_HPP
#define APP_FRONTEND_UI_HELPERS_HPP

#include <cstddef>
#include <vector>
#include <wx/wx.h>
#include <wx/listctrl.h>

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
    int content_px{0};         // ancho del contenido más largo (fuente de la lista)
    int floor_content_px{-1};  // contenido que no puede recortarse (-1: igual a content_px)
    int content_pad{16};       // relleno alrededor del contenido
    int extra_px{0};           // reserva adicional (p. ej. imagen de la fila en Touch)
};

// Reparte los anchos de columna de la lista:
//  - el encabezado (fuente real del encabezado + 20 px escalados) cabe completo en TODAS las columnas;
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
                          const wxFont& font = wxNullFont);

// Habilita o deshabilita el campo; el fondo del contenedor (si lo hay) sigue al del campo.
void set_text_field_enabled(wxTextCtrl* edit, bool enabled);

} // namespace app::frontend::ui

#endif // APP_FRONTEND_UI_HELPERS_HPP
