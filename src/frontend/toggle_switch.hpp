#ifndef APP_FRONTEND_TOGGLE_SWITCH_HPP
#define APP_FRONTEND_TOGGLE_SWITCH_HPP

#include <wx/wx.h>
#include <wx/control.h>

namespace app::frontend {

/**
 * @brief Interruptor deslizante reutilizable (pastilla con perilla).
 *
 * Dibujo con doble búfer sin animaciones ni temporizadores. Emite
 * wxEVT_CHECKBOX al cambiar por interacción del usuario (clic o Espacio),
 * soporta navegación por Tab y responde en Windows a BM_GETCHECK/BM_SETCHECK.
 */
class ToggleSwitch : public wxControl {
public:
    ToggleSwitch(
        wxWindow* parent,
        wxWindowID id = wxID_ANY,
        bool initial_value = false,
        const wxString& accessible_label = wxEmptyString
    );

    bool GetValue() const { return checked_; }
    void SetValue(bool value);
    void Toggle();

    bool Enable(bool enable = true) override;
    bool AcceptsFocus() const override { return IsEnabled(); }
    bool AcceptsFocusFromKeyboard() const override { return IsEnabled(); }

protected:
    wxSize DoGetBestSize() const override;

#ifdef __WXMSW__
    WXLRESULT MSWWindowProc(WXUINT message, WXWPARAM wParam, WXLPARAM lParam) override;
#endif

private:
    void on_paint(wxPaintEvent& event);
    void on_left_down(wxMouseEvent& event);
    void on_left_up(wxMouseEvent& event);
    void on_mouse_capture_lost(wxMouseCaptureLostEvent& event);
    void on_key_down(wxKeyEvent& event);
    void on_focus_changed(wxFocusEvent& event);
    void update_cursor();
    void emit_change_event();

    bool checked_{false};
    bool pressed_{false};
};

} // namespace app::frontend

#endif // APP_FRONTEND_TOGGLE_SWITCH_HPP
