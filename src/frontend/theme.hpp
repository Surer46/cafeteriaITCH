#ifndef APP_FRONTEND_THEME_HPP
#define APP_FRONTEND_THEME_HPP

#include <wx/wx.h>
#include <wx/bmpbndl.h>
#include <string>
#include <map>
#include <tuple>
#include <vector>

namespace app::frontend {

enum class UiProfile {
    Standard,
    Touch
};

class Theme {
public:
    // Paleta oficial según frontend/design-system.md
    static const wxColour ColorBackground;     // #F1F5F9 - Fondo general
    static const wxColour ColorSurface;        // #FFFFFF - Tarjetas, formularios y cuenta
    static const wxColour ColorSurfaceSubtle;  // #F8FAFC - Cabeceras de tabla y grupos
    static const wxColour ColorBorder;         // #CBD5E1 - Separadores/bordes de superficie
    static const wxColour ColorBorderSubtle;   // #E2E8F0 - Bordes suaves interiores
    static const wxColour ColorText;           // #1F2937 - Contenido principal
    static const wxColour ColorTextSecondary;  // #475569 - Etiquetas secundarias
    static const wxColour ColorTextMuted;      // #94A3B8 - Texto atenuado/placeholders
    static const wxColour ColorBrand;          // #B91C1C - Acción de marca y selección
    static const wxColour ColorBrandDeep;      // #991B1B - Sidebar y hover de marca
    static const wxColour ColorBrandSoft;      // #FEF2F2 - Fondo de selección contextual
    static const wxColour ColorAccent;         // #FACC15 - Acento puntual/indicador
    static const wxColour ColorAccentText;     // #78350F - Texto sobre acento
    static const wxColour ColorSuccess;        // #166534 - Cobrar / estado positivo
    static const wxColour ColorSuccessSoft;    // #DCFCE7 - Fondo suave para badges positivos
    static const wxColour ColorError;          // #B91C1C - Error / destructivo
    static const wxColour ColorErrorSoft;      // #FEE2E2 - Fondo suave para errores
    static const wxColour ColorFocus;          // #1D4ED8 - Contorno de foco visible

    // Gestión del ciclo de vida y rutas de recursos locales
    static void init(const std::string& assets_dir = "");
    static void cleanup();
    static std::string get_assets_dir();

    // Coordinación de perfiles (FE-08 + FE-09)
    static UiProfile get_profile();
    static void set_profile(UiProfile profile);
    static bool is_touch();

    // Persistencia de configuración por puesto (FE-08)
    static void set_data_dir(const std::string& data_dir);
    static std::string get_data_dir();
    static void load_station_profile();
    static void save_station_profile(UiProfile profile);

    // Métricas dinámicas por perfil
    static int get_spacing_xs();
    static int get_spacing_sm();
    static int get_spacing_md();
    static int get_spacing_lg();
    static int get_spacing_xl();

    static int get_card_radius();
    static int get_button_radius();
    static int get_control_radius();
    static int get_button_height();
    static int get_primary_button_height();
    static int get_touch_target_min();

    // Fuentes locales (Roboto con fallback al sistema)
    static wxFont get_font(int size_pt, wxFontWeight weight = wxFONTWEIGHT_NORMAL, bool italic = false);
    static wxFont get_font_small(wxFontWeight weight = wxFONTWEIGHT_NORMAL);
    static wxFont get_font_body(wxFontWeight weight = wxFONTWEIGHT_NORMAL);
    static wxFont get_font_subtitle(wxFontWeight weight = wxFONTWEIGHT_BOLD);
    static wxFont get_font_title(wxFontWeight weight = wxFONTWEIGHT_BOLD);
    static wxFont get_font_hero(wxFontWeight weight = wxFONTWEIGHT_BOLD);

    // Iconos locales (Material Symbols SVG con caché por DPI y color)
    static wxBitmapBundle get_icon(const std::string& icon_name, const wxSize& size = wxSize(20, 20),
                                   const wxColour& tint_color = wxNullColour);

    // Estilos de botones estándar
    static void style_button_primary(wxButton* btn, int min_height = -1);
    static void style_button_success(wxButton* btn, int min_height = -1);
    static void style_button_secondary(wxButton* btn, int min_height = -1);
    static void style_button_destructive(wxButton* btn, int min_height = -1);
    static void style_button_brand(wxButton* btn, int min_height = -1);

    // Configuración uniforme de icono, posición y márgenes interiores en botones
    static void setup_button_icon(wxButton* btn, const std::string& icon_name, const wxSize& size, const wxColour& tint = wxNullColour);

private:
    static std::string assets_dir_;
    static std::string data_dir_;
    static UiProfile current_profile_;
    static bool fonts_registered_;
    static std::vector<std::string> registered_font_paths_;

    // Caché de iconos: (name, width, height, hex_color) -> wxBitmapBundle
    using IconKey = std::tuple<std::string, int, int, unsigned long>;
    static std::map<IconKey, wxBitmapBundle> icon_cache_;
};

// =============================================================================
// Componentes visuales reutilizables ligeros
// =============================================================================

// Tarjeta con esquinas redondeadas y borde sutil sin blur ni sombras pesadas
class CardPanel : public wxPanel {
public:
    CardPanel(wxWindow* parent, wxWindowID id = wxID_ANY,
              const wxPoint& pos = wxDefaultPosition,
              const wxSize& size = wxDefaultSize,
              long style = wxTAB_TRAVERSAL);

    void set_corner_radius(int radius);
    void set_card_background(const wxColour& bg);
    void set_card_border(const wxColour& border_col, int border_width = 1);

private:
    void on_paint(wxPaintEvent& evt);

    int corner_radius_{14};
    wxColour bg_colour_{Theme::ColorSurface};
    wxColour border_colour_{Theme::ColorBorder};
    int border_width_{1};
};

// Badge de estado tipo píldora (e.g. "Abierta" / "Cerrada")
class StatusBadge : public wxPanel {
public:
    StatusBadge(wxWindow* parent, const wxString& text, bool is_open = true);

    void set_status(const wxString& text, bool is_open);

protected:
    wxSize DoGetBestSize() const override;

private:
    void on_paint(wxPaintEvent& evt);

    wxString text_;
    bool is_open_{true};
};

} // namespace app::frontend

#endif // APP_FRONTEND_THEME_HPP
