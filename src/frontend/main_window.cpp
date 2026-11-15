#include "src/frontend/main_window.hpp"
#include "src/frontend/backoffice_users_panel.hpp"
#include "src/frontend/backoffice_roles_panel.hpp"
#include "src/frontend/backoffice_print_config_panel.hpp"
#include "src/frontend/backoffice_settings_panel.hpp"
#include "src/frontend/pos_panel.hpp"
#include "src/frontend/cash_shift_panel.hpp"
#include "src/frontend/audit_log_panel.hpp"
#include "src/frontend/inventory_ingredients_panel.hpp"
#include "src/frontend/theme.hpp"
#include <wx/statline.h>

namespace app::frontend {

MainWindow::MainWindow(const services::ServiceRegistry& services)
    : wxFrame(nullptr, wxID_ANY, wxT("Cafetería ITCH II"),
              wxDefaultPosition, wxDefaultSize),
      services_(services),
      key_filter_(std::make_unique<FullscreenKeyFilter>(this)) {
    SetClientSize(FromDIP(wxSize(1200, 720)));
    SetMinClientSize(FromDIP(wxSize(960, 600)));
    setup_ui();
}

MainWindow::~MainWindow() {
    if (key_filter_installed_ && key_filter_) {
        wxEvtHandler::RemoveFilter(key_filter_.get());
        key_filter_installed_ = false;
    }
}

int MainWindow::FullscreenKeyFilter::FilterEvent(wxEvent& event) {
    if (!owner_) return Event_Skip;
    const wxEventType type = event.GetEventType();
    if (type != wxEVT_CHAR_HOOK && type != wxEVT_KEY_DOWN) {
        return Event_Skip;
    }
    auto* key_evt = wxDynamicCast(&event, wxKeyEvent);
    wxWindow* ev_win = wxDynamicCast(event.GetEventObject(), wxWindow);
    if (!key_evt || !ev_win) {
        return Event_Skip;
    }
    if (ev_win != owner_ && wxGetTopLevelParent(ev_win) != owner_) {
        return Event_Skip;
    }
    if (owner_->handle_fullscreen_key(*key_evt)) {
        return Event_Processed;
    }
    return Event_Skip;
}

void MainWindow::update_header_status() {
    std::string shift_status = "Caja Principal · Cerrada";
    if (services_.shift_service) {
        auto shift_res = services_.shift_service->get_current_shift();
        if (shift_res.is_success()) {
            const auto& s = shift_res.value();
            shift_status = s.register_name + " · " + (s.is_open ? "Abierta" : "Cerrada");
        }
    }

    std::string user_info = "Usuario: Cajero 01";
    if (services_.account_service) {
        auto user_res = services_.account_service->current_user();
        if (user_res.is_success()) {
            user_info = "Usuario: " + user_res.value().username;
        }
    }

    if (header_panel_) {
        header_panel_->update_status(shift_status, user_info);
    }
}

void MainWindow::setup_ui() {
    SetBackgroundColour(Theme::ColorBackground);
    auto* root_sizer = new wxBoxSizer(wxVERTICAL);

    // 1. Encabezado superior con estado inicial real y toggle de perfil (FE-08/FE-09)
    header_panel_ = new HeaderPanel(this, "Cafetería ITCH II", "Caja Principal · Cerrada", "Usuario: ...",
        [this](UiProfile) {
            apply_profile_refresh();
        });
    root_sizer->Add(header_panel_, 0, wxEXPAND);
    update_header_status();

    // 2. Área intermedia horizontal: Sidebar (izquierda) + Contenido (derecha)
    auto* body_sizer = new wxBoxSizer(wxHORIZONTAL);

    sidebar_panel_ = new SidebarPanel(this, [this](SectionId section) {
        on_section_changed(section);
    });
    body_sizer->Add(sidebar_panel_, 0, wxEXPAND);

    // Área derecha: Barra de subsecciones + Vista dinámica
    auto* right_column = new wxBoxSizer(wxVERTICAL);

    subnav_bar_ = new SubnavBar(this, [this](const std::string& sub_name) {
        on_subsection_changed(sub_name);
    });
    right_column->Add(subnav_bar_, 0, wxEXPAND);

    content_container_ = new wxPanel(this, wxID_ANY);
    content_container_->SetBackgroundColour(Theme::ColorBackground);
    content_sizer_ = new wxBoxSizer(wxVERTICAL);
    content_container_->SetSizer(content_sizer_);

    right_column->Add(content_container_, 1, wxEXPAND);
    body_sizer->Add(right_column, 1, wxEXPAND);

    root_sizer->Add(body_sizer, 1, wxEXPAND);

    bool is_dev = services_.account_service && services_.account_service->has_permission("backup.dev_config");
    subnav_bar_->set_section(SectionId::PointOfSale, is_dev);

    SetSizer(root_sizer);
    update_content_view();
    Centre();

    Bind(wxEVT_MENU, [this](wxCommandEvent& e) {
        if (current_view_) {
            current_view_->GetEventHandler()->ProcessEvent(e);
        }
    }, 1001);

    saved_window_rect_ = GetRect();
    if (!key_filter_installed_ && key_filter_) {
        wxEvtHandler::AddFilter(key_filter_.get());
        key_filter_installed_ = true;
    }
    if (Theme::get_fullscreen()) {
        set_fullscreen_mode(true);
    }
}

bool MainWindow::has_open_modal_dialog() const {
#ifdef __WXMSW__
    HWND hwnd = static_cast<HWND>(GetHWND());
    if (hwnd && !::IsWindowEnabled(hwnd)) {
        return true;
    }
#endif
    for (wxWindow* w : wxTopLevelWindows) {
        if (!w || w == this || !w->IsShown()) continue;
        if (auto* dlg = wxDynamicCast(w, wxDialog)) {
            if (dlg->IsModal()) {
                return true;
            }
        }
    }
    return false;
}

bool MainWindow::handle_fullscreen_key(wxKeyEvent& evt) {
    if (evt.GetKeyCode() == WXK_F11 && !evt.HasAnyModifiers() && !evt.IsAutoRepeat()) {
        if (!has_open_modal_dialog()) {
            toggle_fullscreen();
        }
        return true;
    }
    return false;
}

void MainWindow::toggle_fullscreen() {
    set_fullscreen_mode(!IsFullScreen());
}

void MainWindow::set_fullscreen_mode(bool fullscreen) {
    if (fullscreen == IsFullScreen()) {
        Theme::save_station_fullscreen(fullscreen);
        sync_fullscreen_ui();
        return;
    }

    if (fullscreen) {
        saved_was_maximized_ = IsMaximized();
        if (!saved_was_maximized_) {
            saved_window_rect_ = GetRect();
        }
        ShowFullScreen(true, wxFULLSCREEN_ALL);
    } else {
        ShowFullScreen(false);
        if (saved_was_maximized_) {
            if (!IsMaximized()) {
                Maximize(true);
            }
        } else if (!saved_window_rect_.IsEmpty()) {
            if (IsMaximized()) {
                Maximize(false);
            }
            if (GetRect() != saved_window_rect_) {
                SetSize(saved_window_rect_);
            }
        }
    }

    Theme::save_station_fullscreen(fullscreen);
    Layout();
    if (content_container_) {
        content_container_->Layout();
    }
    if (current_view_) {
        current_view_->Layout();
    }
    sync_fullscreen_ui();
}

void MainWindow::sync_fullscreen_ui() {
    if (settings_panel_) {
        settings_panel_->sync_fullscreen_selection();
    }
}

void MainWindow::apply_profile_refresh() {
    if (header_panel_) {
        header_panel_->sync_profile_button();
    }
    if (sidebar_panel_) {
        sidebar_panel_->refresh_profile();
    }
    on_section_changed(current_section_);
    Layout();
}

void MainWindow::schedule_profile_refresh() {
    CallAfter([this]() {
        apply_profile_refresh();
    });
}

void MainWindow::on_section_changed(SectionId section) {
    current_section_ = section;
    bool is_dev = services_.account_service && services_.account_service->has_permission("backup.dev_config");
    subnav_bar_->set_section(section, is_dev);
    current_subsection_ = subnav_bar_->active_subsection();
    update_content_view();
}

void MainWindow::on_subsection_changed(const std::string& sub_name) {
    current_subsection_ = sub_name;
    update_content_view();
}

void MainWindow::update_content_view() {
    settings_panel_ = nullptr;
    content_sizer_->Clear(true);
    current_view_ = nullptr;

    // Validación de seguridad para "Servidor de respaldos"
    if (current_subsection_ == "Servidor de respaldos") {
        auto config_res = services_.backup_service->get_developer_server_config();
        if (!config_res.is_success()) {
            current_view_ = create_placeholder_view(
                content_container_,
                "Acceso Denegado",
                "Error 403: " + config_res.error_message()
            );
            content_sizer_->Add(current_view_, 1, wxEXPAND | wxALL, FromDIP(12));
            content_container_->Layout();
            return;
        }
    }

    if (current_section_ == SectionId::PointOfSale && current_subsection_ == "Venta") {
        current_view_ = new PosPanel(content_container_, services_);
    } else if (current_section_ == SectionId::Inventory && current_subsection_ == "Ingredientes") {
        current_view_ = new InventoryIngredientsPanel(
            content_container_, services_.inventory_service, services_.account_service);
    } else if (current_section_ == SectionId::CashAndReports && current_subsection_ == "Caja y turnos") {
        current_view_ = new CashShiftPanel(content_container_, services_, [this]() {
            update_header_status();
        });
    } else if (current_section_ == SectionId::CashAndReports && current_subsection_ == "Bitácora") {
        current_view_ = new AuditLogPanel(content_container_, services_.account_service);
    } else if (current_section_ == SectionId::BackOffice && current_subsection_ == "Usuarios y cuentas") {
        current_view_ = new BackofficeUsersPanel(content_container_, services_.account_service);
    } else if (current_section_ == SectionId::BackOffice && current_subsection_ == "Roles y permisos") {
        current_view_ = new BackofficeRolesPanel(content_container_, services_.account_service);
    } else if (current_section_ == SectionId::BackOffice && (current_subsection_ == "Configuración" || current_subsection_ == "Impresión")) {
        settings_panel_ = new BackofficeSettingsPanel(
            content_container_,
            services_,
            [this](UiProfile) {
                schedule_profile_refresh();
            },
            [this](bool full) {
                set_fullscreen_mode(full);
            }
        );
        current_view_ = settings_panel_;
    } else {
        std::string section_label;
        switch (current_section_) {
            case SectionId::PointOfSale: section_label = "Punto de Venta"; break;
            case SectionId::Inventory: section_label = "Inventario"; break;
            case SectionId::CashAndReports: section_label = "Caja y Reportes"; break;
            case SectionId::BackOffice: section_label = "BackOffice"; break;
        }
        std::string desc = "Módulo en fase de preparación de contratos (FE-00 / BE-00).";
        current_view_ = create_placeholder_view(content_container_, section_label + " > " + current_subsection_, desc);
    }

    content_sizer_->Add(current_view_, 1, wxEXPAND | wxALL, FromDIP(12));
    content_container_->Layout();
}

wxPanel* MainWindow::create_placeholder_view(wxWindow* parent, const std::string& title, const std::string& desc) {
    auto* panel = new CardPanel(parent, wxID_ANY);
    panel->set_card_background(Theme::ColorSurface);
    auto* sizer = new wxBoxSizer(wxVERTICAL);

    sizer->AddSpacer(FromDIP(20));

    auto* t = new wxStaticText(panel, wxID_ANY, wxString::FromUTF8(title.c_str()));
    t->SetFont(Theme::get_font_title(wxFONTWEIGHT_BOLD));
    t->SetForegroundColour(Theme::ColorBrandDeep);
    sizer->Add(t, 0, wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(16));

    auto* d = new wxStaticText(panel, wxID_ANY, wxString::FromUTF8(desc.c_str()));
    d->SetFont(Theme::get_font_body());
    d->SetForegroundColour(Theme::ColorText);
    sizer->Add(d, 0, wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(16));

    auto* note = new wxStaticText(panel, wxID_ANY,
        wxT("Base integrable preparada: contratos compartidos, almacenamiento SQLite embebido\n")
        wxT("y componentes visuales nativos conforme al sistema de diseño Cupertino / ITCH II."));
    note->SetFont(Theme::get_font_small());
    note->SetForegroundColour(Theme::ColorTextSecondary);
    sizer->Add(note, 0, wxLEFT | wxRIGHT, FromDIP(16));

    sizer->AddStretchSpacer(1);
    panel->SetSizer(sizer);
    return panel;
}

} // namespace app::frontend
