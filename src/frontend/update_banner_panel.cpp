#include "src/frontend/update_banner_panel.hpp"
#include "src/frontend/theme.hpp"
#include <algorithm>
#include <wx/statline.h>

namespace app::frontend {

namespace {

wxColour warning_banner_bg() {
    return wxColour(0xFE, 0xF3, 0xC7);
}

wxColour info_banner_bg() {
    return wxColour(0xEF, 0xF6, 0xFF);
}

int compute_percent(std::int64_t done, std::int64_t total) {
    if (total <= 0 || done <= 0) {
        return 0;
    }
    if (done >= total) {
        return 100;
    }
    return static_cast<int>((done * 100) / total);
}

} // namespace

UpdateBannerPanel::UpdateBannerPanel(
    wxWindow* parent,
    std::function<void()> on_view_notes,
    std::function<void()> on_dismiss_available,
    std::function<void()> on_cancel_ready,
    std::function<void()> on_ack_result
) : wxPanel(parent, wxID_ANY),
    on_view_notes_(std::move(on_view_notes)),
    on_dismiss_available_(std::move(on_dismiss_available)),
    on_cancel_ready_(std::move(on_cancel_ready)),
    on_ack_result_(std::move(on_ack_result)) {
    setup_ui();
    Hide();
}

int UpdateBannerPanel::banner_button_height() {
    return Theme::is_touch() ? 48 : 34;
}

void UpdateBannerPanel::setup_ui() {
    SetBackgroundColour(info_banner_bg());
    auto* outer = new wxBoxSizer(wxVERTICAL);

    auto* row = new wxBoxSizer(wxHORIZONTAL);
    lbl_message_ = new wxStaticText(this, wxID_ANY, wxEmptyString);
    lbl_message_->SetFont(Theme::get_font_body(wxFONTWEIGHT_MEDIUM));
    lbl_message_->SetForegroundColour(Theme::ColorText);
    row->Add(lbl_message_, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(12));

    const int btn_h = banner_button_height();
    btn_primary_ = new wxButton(this, wxID_ANY, wxT("Ver novedades"));
    Theme::style_button_primary(btn_primary_, btn_h);
    Theme::ensure_button_min_size(btn_primary_, 130, btn_h);
    row->Add(btn_primary_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));

    btn_secondary_ = new wxButton(this, wxID_ANY, wxT("Ahora no"));
    Theme::style_button_secondary(btn_secondary_, btn_h);
    Theme::ensure_button_min_size(btn_secondary_, 110, btn_h);
    row->Add(btn_secondary_, 0, wxALIGN_CENTER_VERTICAL);

    outer->Add(row, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP | wxBOTTOM, FromDIP(8));
    outer->Add(new wxStaticLine(this, wxID_ANY), 0, wxEXPAND);
    SetSizer(outer);

    btn_primary_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        if (mode_ == Mode::Available || mode_ == Mode::Downloading) {
            if (on_view_notes_) {
                on_view_notes_();
            }
        } else if (mode_ == Mode::Ready) {
            if (on_cancel_ready_) {
                on_cancel_ready_();
            }
        } else if (mode_ == Mode::InstallResult) {
            if (on_ack_result_) {
                on_ack_result_();
            }
        }
    });

    btn_secondary_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        if (mode_ == Mode::Available) {
            if (on_dismiss_available_) {
                on_dismiss_available_();
            }
        }
    });
}

void UpdateBannerPanel::refresh_profile() {
    const int btn_h = banner_button_height();
    lbl_message_->SetFont(Theme::get_font_body(wxFONTWEIGHT_MEDIUM));
    if (mode_ == Mode::Available || mode_ == Mode::Downloading) {
        Theme::style_button_primary(btn_primary_, btn_h);
    } else {
        Theme::style_button_secondary(btn_primary_, btn_h);
    }
    Theme::ensure_button_min_size(btn_primary_, 130, btn_h);
    Theme::style_button_secondary(btn_secondary_, btn_h);
    Theme::ensure_button_min_size(btn_secondary_, 110, btn_h);
    apply_layout_refresh();
}

void UpdateBannerPanel::apply_layout_refresh() {
    if (IsShown()) {
        const int avail_w = GetSize().GetWidth() > FromDIP(320) ? GetSize().GetWidth() - FromDIP(300) : FromDIP(500);
        lbl_message_->Wrap(std::max(FromDIP(260), avail_w));
    }
    Layout();
    if (wxWindow* parent = GetParent()) {
        parent->Layout();
    }
}

void UpdateBannerPanel::clear_banner() {
    mode_ = Mode::Hidden;
    if (IsShown()) {
        Hide();
        apply_layout_refresh();
    }
}

void UpdateBannerPanel::show_install_result(
    const contracts::UpdateInstallResultDto& result,
    const std::string& installed_version
) {
    mode_ = Mode::InstallResult;
    const std::string effective_reason = result.reason.empty() ? "NOT_COMPLETED" : result.reason;
    const wxString ver_wx = wxString::FromUTF8(
        (result.version.empty() ? installed_version : result.version).c_str()
    );
    const wxString inst_wx = wxString::FromUTF8(installed_version.c_str());

    if (effective_reason == "INSTALLED") {
        SetBackgroundColour(Theme::ColorSuccessSoft);
        lbl_message_->SetForegroundColour(Theme::ColorSuccess);
    } else {
        SetBackgroundColour(warning_banner_bg());
        lbl_message_->SetForegroundColour(Theme::ColorAccentText);
    }

    wxString msg = wxString::FromUTF8(result.message.c_str());
    if (msg.empty()) {
        if (effective_reason == "INSTALLED") {
            msg = wxString::Format(wxT("Se actualizó a la versión %s."), ver_wx);
        } else if (effective_reason == "PERMISSION_DENIED") {
            msg = wxT("No se instaló porque no se dio permiso en Windows. Se volverá a ofrecer.");
        } else if (effective_reason == "LAUNCH_FAILED") {
            msg = wxString::Format(
                wxT("No se pudo iniciar la instalación. El programa sigue en la versión %s y se volverá a ofrecer."),
                inst_wx
            );
        } else {
            msg = wxString::Format(
                wxT("La actualización no se completó; sigues con la versión %s. Se volverá a ofrecer."),
                inst_wx
            );
        }
    }

    lbl_message_->SetLabel(msg);
    const int btn_h = banner_button_height();
    btn_primary_->SetLabel(wxT("Entendido"));
    Theme::style_button_secondary(btn_primary_, btn_h);
    Theme::ensure_button_min_size(btn_primary_, 120, btn_h);
    btn_primary_->Show();
    btn_secondary_->Hide();
    Show();
    apply_layout_refresh();
}

void UpdateBannerPanel::show_status(const contracts::UpdateStatusDto& status, bool dismissed_this_session) {
    if (status.state == "NOT_CONFIGURED" || status.state == "UP_TO_DATE" ||
        status.state == "CHECKING" || status.state == "ERROR") {
        clear_banner();
        return;
    }

    const int btn_h = banner_button_height();
    const wxString ver_wx = wxString::FromUTF8(status.available_version.c_str());

    if (status.state == "READY" && status.accepted) {
        mode_ = Mode::Ready;
        SetBackgroundColour(Theme::ColorSuccessSoft);
        lbl_message_->SetForegroundColour(Theme::ColorSuccess);
        lbl_message_->SetLabel(
            wxString::Format(wxT("La versión %s se instalará al cerrar el programa."), ver_wx)
        );
        btn_primary_->SetLabel(wxT("Cancelar"));
        Theme::style_button_secondary(btn_primary_, btn_h);
        Theme::ensure_button_min_size(btn_primary_, 120, btn_h);
        btn_primary_->Show();
        btn_secondary_->Hide();
        Show();
        apply_layout_refresh();
        return;
    }

    if (dismissed_this_session) {
        clear_banner();
        return;
    }

    if (status.state == "DOWNLOADING") {
        mode_ = Mode::Downloading;
        SetBackgroundColour(info_banner_bg());
        lbl_message_->SetForegroundColour(Theme::ColorBrandDeep);
        const int pct = compute_percent(status.downloaded_bytes, status.installer_size_bytes);
        lbl_message_->SetLabel(
            wxString::Format(wxT("Descargando la versión %s (%d %%)..."), ver_wx, pct)
        );
        btn_primary_->SetLabel(wxT("Ver novedades"));
        Theme::style_button_primary(btn_primary_, btn_h);
        Theme::ensure_button_min_size(btn_primary_, 140, btn_h);
        btn_primary_->Show();
        btn_secondary_->Hide();
        Show();
        apply_layout_refresh();
        return;
    }

    if (status.state == "AVAILABLE" && !status.available_version.empty()) {
        mode_ = Mode::Available;
        SetBackgroundColour(info_banner_bg());
        lbl_message_->SetForegroundColour(Theme::ColorBrandDeep);
        lbl_message_->SetLabel(
            wxString::Format(wxT("Hay una versión nueva del programa (%s)."), ver_wx)
        );
        btn_primary_->SetLabel(wxT("Ver novedades"));
        Theme::style_button_primary(btn_primary_, btn_h);
        Theme::ensure_button_min_size(btn_primary_, 140, btn_h);
        btn_primary_->Show();

        btn_secondary_->SetLabel(wxT("Ahora no"));
        Theme::style_button_secondary(btn_secondary_, btn_h);
        Theme::ensure_button_min_size(btn_secondary_, 110, btn_h);
        btn_secondary_->Show();

        Show();
        apply_layout_refresh();
        return;
    }

    clear_banner();
}

} // namespace app::frontend
