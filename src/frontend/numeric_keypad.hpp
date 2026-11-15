#ifndef APP_FRONTEND_NUMERIC_KEYPAD_HPP
#define APP_FRONTEND_NUMERIC_KEYPAD_HPP

#include <wx/eventfilter.h>
#include <wx/wx.h>
#include <functional>
#include <memory>
#include <vector>

namespace app::frontend::ui {

/// Teclado numérico en pantalla para perfil Táctil.
/// Solo inserta o borra texto en el wxTextCtrl activo sin calcular importes ni perder el foco.
class NumericKeypad final : public wxPanel {
public:
    using ProtectedRectsFn = std::function<std::vector<wxRect>()>;

    /// Constructor en modo flotante sobre un panel anfitrión (oculto inicialmente).
    explicit NumericKeypad(wxWindow* host);

    /// Constructor en modo integrado dentro de un sizer (p. ej. diálogo de cobro o editar cantidad).
    NumericKeypad(wxWindow* parent, bool allow_decimal, bool compact = false);

    ~NumericKeypad() override;

    /// Vincula un campo numérico al teclado flotante en perfil Táctil.
    /// @param field Campo wxTextCtrl destino.
    /// @param hit_box Contenedor visual del campo (o nullptr si no usa caja externa).
    /// @param allow_decimal true para campos de dinero; false para enteros (oculta '.').
    /// @param anchor_control Control de referencia para ubicar el teclado (p. ej. field o su fila).
    /// @param protected_fn Devuelve rectángulos en coordenadas cliente del host que nunca deben taparse.
    void bind_field(
        wxTextCtrl* field,
        wxWindow* hit_box,
        bool allow_decimal,
        wxWindow* anchor_control = nullptr,
        ProtectedRectsFn protected_fn = nullptr
    );

    /// En modo integrado, asigna el campo destino.
    void set_target_field(wxTextCtrl* field, bool hide_on_done = false);

    void set_allow_decimal(bool allow);
    void set_compact(bool compact);
    bool is_compact() const { return compact_; }

    /// Muestra el teclado flotante para el campo indicado y lo ubica sin tapar zonas protegidas.
    void show_for_field(wxTextCtrl* field);

    /// Oculta el teclado. Si restore_focus es true (Listo), conserva foco y cursor en el campo.
    void hide_keypad(bool restore_focus = false);

    /// Recalcula la posición del teclado flotante si está visible.
    void reposition();

    wxTextCtrl* active_field() const { return active_field_; }

    wxSize DoGetBestSize() const override;

private:
    struct BoundField {
        wxTextCtrl* field{nullptr};
        wxWindow* hit_box{nullptr};
        wxWindow* anchor{nullptr};
        bool allow_decimal{false};
        ProtectedRectsFn protected_fn;
    };

    class OutsideClickFilter final : public wxEventFilter {
    public:
        explicit OutsideClickFilter(NumericKeypad* owner) : owner_(owner) {}
        int FilterEvent(wxEvent& event) override;
    private:
        NumericKeypad* owner_{nullptr};
    };

    void create_controls();
    void apply_layout();
    void on_paint(wxPaintEvent& evt);
    void on_key_digit(const wxString& ch);
    void on_key_decimal();
    void on_key_backspace();
    void on_key_clear();
    void on_key_done();
    void restore_target_focus(long new_pos = -1);
    void install_filter();
    void uninstall_filter();
    void raise_over_siblings();
    const BoundField* find_binding(const wxTextCtrl* field) const;
    static wxSize size_for_mode(const wxWindow* w, bool compact);
    static bool intersects_any(const wxRect& candidate, const std::vector<wxRect>& rects);

    bool embedded_{false};
    bool hide_on_done_{true};
    bool allow_decimal_{true};
    bool compact_{false};
    bool suppress_reopen_on_focus_{false};
    bool filter_installed_{false};

    wxWindow* host_{nullptr};
    wxTextCtrl* active_field_{nullptr};
    wxWindow* active_hit_box_{nullptr};
    long last_caret_from_{0};
    long last_caret_to_{0};

    std::vector<BoundField> bindings_;
    std::unique_ptr<OutsideClickFilter> filter_;

    wxButton* btn_digits_[10]{};
    wxButton* btn_dot_{nullptr};
    wxButton* btn_backspace_{nullptr};
    wxButton* btn_clear_{nullptr};
    wxButton* btn_done_{nullptr};
};

} // namespace app::frontend::ui

#endif // APP_FRONTEND_NUMERIC_KEYPAD_HPP
