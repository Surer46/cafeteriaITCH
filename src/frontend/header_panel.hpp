#ifndef APP_FRONTEND_HEADER_PANEL_HPP
#define APP_FRONTEND_HEADER_PANEL_HPP

#include <wx/wx.h>
#include <string>
#include <functional>
#include "theme.hpp"

namespace app::frontend {

class HeaderPanel : public wxPanel {
public:
    HeaderPanel(wxWindow* parent, const std::string& app_title,
                const std::string& shift_status, const std::string& user_name,
                std::function<void(UiProfile)> on_profile_changed = nullptr);

    void update_status(const std::string& shift_status, const std::string& user_name);
    void sync_profile_button();

private:
    void on_paint(wxPaintEvent& evt);
    void on_toggle_profile(wxCommandEvent& evt);

    std::string app_title_;
    std::string shift_status_;
    std::string user_name_;
    std::function<void(UiProfile)> on_profile_changed_;

    wxStaticText* title_text_{nullptr};
    StatusBadge* shift_badge_{nullptr};
    wxStaticBitmap* user_avatar_{nullptr};
    wxStaticText* user_text_{nullptr};
    wxButton* profile_toggle_btn_{nullptr};
};

} // namespace app::frontend

#endif // APP_FRONTEND_HEADER_PANEL_HPP
