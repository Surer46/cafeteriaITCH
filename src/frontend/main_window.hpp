#ifndef APP_FRONTEND_MAIN_WINDOW_HPP
#define APP_FRONTEND_MAIN_WINDOW_HPP

#include <wx/wx.h>
#include <wx/eventfilter.h>
#include <wx/listctrl.h>
#include <wx/timer.h>
#include <wx/weakref.h>
#include <memory>
#include <string>
#include "src/frontend/header_panel.hpp"
#include "src/frontend/sidebar_panel.hpp"
#include "src/frontend/subnav_bar.hpp"
#include "src/frontend/update_banner_panel.hpp"
#include "app/services/mock_services.hpp"

namespace app::frontend {

class BackofficeSettingsPanel;
class BackofficeUpdatesPanel;
class UpdateNotesDialog;

class MainWindow : public wxFrame {
public:
    explicit MainWindow(const services::ServiceRegistry& services);
    ~MainWindow() override;

    void set_fullscreen_mode(bool fullscreen);
    void toggle_fullscreen();

private:
    class FullscreenKeyFilter final : public wxEventFilter {
    public:
        explicit FullscreenKeyFilter(MainWindow* owner) : owner_(owner) {}
        int FilterEvent(wxEvent& event) override;
    private:
        MainWindow* owner_{nullptr};
    };

    void setup_ui();
    bool handle_fullscreen_key(wxKeyEvent& evt);
    bool has_open_modal_dialog() const;
    void sync_fullscreen_ui();
    void on_section_changed(SectionId section);
    void on_subsection_changed(const std::string& sub_name);
    void update_content_view();
    void update_header_status();
    void apply_profile_refresh();
    void schedule_profile_refresh();
    void init_update_notifications();
    void refresh_update_state();
    void open_update_notes_dialog();
    void on_update_poll_timer(wxTimerEvent& evt);
    void on_close(wxCloseEvent& evt);

    wxPanel* create_placeholder_view(wxWindow* parent, const std::string& title, const std::string& desc);

    services::ServiceRegistry services_;

    HeaderPanel* header_panel_{nullptr};
    UpdateBannerPanel* update_banner_{nullptr};
    SidebarPanel* sidebar_panel_{nullptr};
    SubnavBar* subnav_bar_{nullptr};
    wxPanel* content_container_{nullptr};
    wxBoxSizer* content_sizer_{nullptr};
    wxPanel* current_view_{nullptr};
    BackofficeSettingsPanel* settings_panel_{nullptr};
    BackofficeUpdatesPanel* updates_panel_{nullptr};
    wxWeakRef<UpdateNotesDialog> notes_dialog_;
    wxTimer update_poll_timer_;
    bool destroying_{false};
    bool banner_dismissed_this_session_{false};
    std::string dismissed_version_;
    bool install_result_visible_{false};

    wxRect saved_window_rect_{};
    bool saved_was_maximized_{false};
    std::unique_ptr<FullscreenKeyFilter> key_filter_;
    bool key_filter_installed_{false};

    SectionId current_section_{SectionId::PointOfSale};
    std::string current_subsection_{"Venta"};
};

} // namespace app::frontend

#endif // APP_FRONTEND_MAIN_WINDOW_HPP
