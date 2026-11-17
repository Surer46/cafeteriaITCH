#include "src/frontend/update_notes_dialog.hpp"
#include <algorithm>
#include <string>

namespace app::frontend {

namespace {

constexpr int kDialogPollIntervalMs = 1500;

int compute_download_percent(std::int64_t done, std::int64_t total) {
    if (total <= 0 || done <= 0) {
        return 0;
    }
    if (done >= total) {
        return 100;
    }
    return static_cast<int>((done * 100) / total);
}

bool is_verification_rejection(const contracts::UpdateStatusDto& st) {
    if (st.error_kind == "INVALID") {
        return true;
    }
    return st.error_message.find("dañada") != std::string::npos ||
           st.error_message.find("descartó") != std::string::npos ||
           st.error_message.find("no se pudo comprobar") != std::string::npos ||
           st.error_message.find("auténtica") != std::string::npos;
}

void set_wrapped_static_text(wxStaticText* lbl, const wxString& text, int wrap_w) {
    if (!lbl) {
        return;
    }
    lbl->SetMinSize(wxDefaultSize);
    lbl->SetLabel(text);
    if (wrap_w > 1) {
        // wxStaticTextBase::Wrap() puede omitir el ajuste si width == m_currentWrap tras SetLabel().
        lbl->Wrap(wrap_w - 1);
        lbl->Wrap(wrap_w);
    }
    lbl->InvalidateBestSize();
    const wxSize best = lbl->GetBestSize();
    if (best.GetHeight() > 0) {
        lbl->SetMinSize(wxSize(-1, best.GetHeight()));
    }
}

} // namespace

UpdateNotesDialog::UpdateNotesDialog(
    wxWindow* parent,
    std::shared_ptr<contracts::IUpdateService> update_service,
    std::function<void()> on_state_changed,
    std::function<void()> on_closed
) : wxDialog(parent, wxID_ANY, wxT("Novedades de la versión"),
             wxDefaultPosition, wxDefaultSize,
             wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
    update_service_(std::move(update_service)),
    on_state_changed_(std::move(on_state_changed)),
    on_closed_(std::move(on_closed)),
    poll_timer_(this) {
    SetBackgroundColour(Theme::ColorBackground);
    Bind(wxEVT_TIMER, &UpdateNotesDialog::on_poll_timer, this, poll_timer_.GetId());
    Bind(wxEVT_CLOSE_WINDOW, &UpdateNotesDialog::on_close_window, this);
    setup_ui();
    refresh_status();
    CentreOnParent();
}

UpdateNotesDialog::~UpdateNotesDialog() {
    destroying_ = true;
    poll_timer_.Stop();
    DestroyChildren();
}

wxString UpdateNotesDialog::format_size_mb(std::int64_t bytes) {
    if (bytes <= 0) {
        return wxT("No disponible");
    }
    constexpr std::int64_t kOneMb = 1024LL * 1024LL;
    std::int64_t centi = (bytes * 100 + (kOneMb / 2)) / kOneMb;
    if (centi < 1) {
        centi = 1;
    }
    return wxString::Format(wxT("%lld.%02lld MB"), centi / 100, centi % 100);
}

void UpdateNotesDialog::setup_ui() {
    const bool touch = Theme::is_touch();
    const int btn_h = touch ? 50 : 38;
    const int dlg_w = FromDIP(touch ? 680 : 580);
    const int dlg_h = FromDIP(touch ? 540 : 470);

    auto* root = new wxBoxSizer(wxVERTICAL);

    lbl_heading_ = new wxStaticText(this, wxID_ANY, wxT("Novedades de la actualización"));
    lbl_heading_->SetFont(Theme::get_font_title(wxFONTWEIGHT_BOLD));
    lbl_heading_->SetForegroundColour(Theme::ColorBrandDeep);
    root->Add(lbl_heading_, 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(18));

    lbl_size_ = new wxStaticText(this, wxID_ANY, wxT("Tamaño de la descarga: —"));
    lbl_size_->SetFont(Theme::get_font_body(wxFONTWEIGHT_MEDIUM));
    lbl_size_->SetForegroundColour(Theme::ColorTextSecondary);
    root->Add(lbl_size_, 0, wxLEFT | wxRIGHT, FromDIP(18));
    root->AddSpacer(FromDIP(2));

    auto* card_notes = new CardPanel(this);
    card_notes->set_card_background(Theme::ColorSurface);
    auto* notes_card_sizer = new wxBoxSizer(wxVERTICAL);

    auto* lbl_notes_cap = new wxStaticText(card_notes, wxID_ANY, wxT("Lo que cambia en esta versión:"));
    lbl_notes_cap->SetFont(Theme::get_font_subtitle(wxFONTWEIGHT_BOLD));
    lbl_notes_cap->SetForegroundColour(Theme::ColorText);
    notes_card_sizer->Add(lbl_notes_cap, 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(14));

    notes_scroll_ = new wxScrolledWindow(card_notes, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL);
    notes_scroll_->SetBackgroundColour(Theme::ColorSurface);
    notes_scroll_->SetScrollRate(0, FromDIP(14));
    notes_sizer_ = new wxBoxSizer(wxVERTICAL);
    notes_scroll_->SetSizer(notes_sizer_);
    notes_card_sizer->Add(notes_scroll_, 1, wxEXPAND | wxALL, FromDIP(12));

    card_notes->SetSizer(notes_card_sizer);
    root->Add(card_notes, 1, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(14));

    lbl_install_note_ = new wxStaticText(
        this,
        wxID_ANY,
        wxT("Se instalará cuando cierres el programa; Windows pedirá permiso para hacer cambios. Tus datos se conservan.")
    );
    lbl_install_note_->SetFont(Theme::get_font_body());
    lbl_install_note_->SetForegroundColour(Theme::ColorTextSecondary);
    set_wrapped_static_text(
        lbl_install_note_,
        wxT("Se instalará cuando cierres el programa; Windows pedirá permiso para hacer cambios. Tus datos se conservan."),
        dlg_w - FromDIP(48)
    );
    root->Add(lbl_install_note_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(12));

    // Área de progreso o listo
    card_progress_ = new CardPanel(this);
    card_progress_->set_card_background(Theme::ColorSurface);
    auto* prog_sizer = new wxBoxSizer(wxVERTICAL);

    lbl_progress_ = new wxStaticText(card_progress_, wxID_ANY, wxEmptyString);
    lbl_progress_->SetFont(Theme::get_font_body(wxFONTWEIGHT_BOLD));
    lbl_progress_->SetForegroundColour(Theme::ColorBrandDeep);
    prog_sizer->Add(lbl_progress_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(12));

    gauge_progress_ = new wxGauge(card_progress_, wxID_ANY, 100, wxDefaultPosition, wxSize(-1, FromDIP(18)));
    prog_sizer->Add(gauge_progress_, 0, wxEXPAND | wxALL, FromDIP(12));

    card_progress_->SetSizer(prog_sizer);
    card_progress_->Hide();
    root->Add(card_progress_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(12));

    // Área de error de descarga / verificación
    pnl_error_ = new wxPanel(this, wxID_ANY);
    pnl_error_->SetBackgroundColour(Theme::ColorErrorSoft);
    auto* err_sizer = new wxBoxSizer(wxVERTICAL);
    lbl_error_ = new wxStaticText(pnl_error_, wxID_ANY, wxEmptyString);
    lbl_error_->SetFont(Theme::get_font_body(wxFONTWEIGHT_MEDIUM));
    lbl_error_->SetForegroundColour(Theme::ColorError);
    err_sizer->Add(lbl_error_, 0, wxEXPAND | wxALL, FromDIP(12));
    pnl_error_->SetSizer(err_sizer);
    pnl_error_->Hide();
    root->Add(pnl_error_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(12));

    auto* btn_row = new wxBoxSizer(wxHORIZONTAL);
    btn_row->AddStretchSpacer(1);

    btn_secondary_ = new wxButton(this, wxID_ANY, wxT("Ahora no"));
    Theme::style_button_secondary(btn_secondary_, btn_h);
    Theme::ensure_button_min_size(btn_secondary_, 130, btn_h);
    btn_secondary_->Bind(wxEVT_BUTTON, &UpdateNotesDialog::on_secondary_action, this);
    btn_row->Add(btn_secondary_, 0, wxRIGHT, FromDIP(10));

    btn_primary_ = new wxButton(this, wxID_ANY, wxT("Descargar e instalar al cerrar"));
    Theme::style_button_primary(btn_primary_, btn_h);
    Theme::ensure_button_min_size(btn_primary_, 240, btn_h);
    btn_primary_->Bind(wxEVT_BUTTON, &UpdateNotesDialog::on_primary_action, this);
    btn_row->Add(btn_primary_, 0);

    root->Add(btn_row, 0, wxEXPAND | wxALL, FromDIP(16));

    SetSizer(root);
    SetMinSize(wxSize(FromDIP(touch ? 620 : 520), FromDIP(touch ? 460 : 400)));
    SetClientSize(wxSize(dlg_w, dlg_h));
}

void UpdateNotesDialog::rebuild_notes(const contracts::UpdateStatusDto& st) {
    notes_sizer_->Clear(true);
    const int client_w = notes_scroll_->GetClientSize().GetWidth();
    const int wrap_w = std::max(FromDIP(280), (client_w > 0 ? client_w : FromDIP(480)) - FromDIP(24));

    if (st.notes.empty()) {
        auto* lbl = new wxStaticText(notes_scroll_, wxID_ANY, wxT("• Mejoras generales del programa."));
        lbl->SetFont(Theme::get_font_body());
        lbl->SetForegroundColour(Theme::ColorText);
        lbl->Wrap(wrap_w);
        notes_sizer_->Add(lbl, 0, wxBOTTOM, FromDIP(6));
    } else {
        for (const auto& line : st.notes) {
            const wxString item_wx = wxT("• ") + wxString::FromUTF8(line.c_str());
            auto* lbl = new wxStaticText(notes_scroll_, wxID_ANY, item_wx);
            lbl->SetFont(Theme::get_font_body());
            lbl->SetForegroundColour(Theme::ColorText);
            lbl->Wrap(wrap_w);
            notes_sizer_->Add(lbl, 0, wxBOTTOM, FromDIP(6));
        }
    }

    notes_scroll_->FitInside();
    notes_scroll_->Layout();
}

void UpdateNotesDialog::refresh_status() {
    if (destroying_ || IsBeingDeleted() || !update_service_) {
        return;
    }
    const auto res = update_service_->get_update_status();
    if (!res.is_success()) {
        poll_timer_.Stop();
        return;
    }
    const std::string prev_ver = last_status_.available_version;
    const auto prev_notes = last_status_.notes;
    last_status_ = res.value();
    if (prev_ver != last_status_.available_version || prev_notes != last_status_.notes) {
        rebuild_notes(last_status_);
    }
    apply_status(last_status_);
}

void UpdateNotesDialog::refresh_profile() {
    if (destroying_ || IsBeingDeleted()) {
        return;
    }
    apply_status(last_status_);
}

void UpdateNotesDialog::apply_status(const contracts::UpdateStatusDto& st) {
    if (destroying_ || IsBeingDeleted()) {
        return;
    }
    const bool touch = Theme::is_touch();
    const int btn_h = touch ? 50 : 38;
    const int wrap_w = std::max(FromDIP(280), GetClientSize().GetWidth() - FromDIP(48));

    const wxString ver_wx = wxString::FromUTF8(st.available_version.c_str());
    if (!ver_wx.empty()) {
        SetTitle(wxString::Format(wxT("Novedades de la versión %s"), ver_wx));
        lbl_heading_->SetLabel(wxString::Format(wxT("Versión %s"), ver_wx));
    } else {
        SetTitle(wxT("Novedades de la versión"));
        lbl_heading_->SetLabel(wxT("Actualización del programa"));
    }

    if (st.is_portable) {
        poll_timer_.Stop();
        lbl_size_->SetLabel(wxString::Format(wxT("Tamaño de la descarga: %s"), format_size_mb(st.installer_size_bytes)));
        set_wrapped_static_text(
            lbl_install_note_,
            wxT("Esta copia es portable: no se actualiza sola. Descarga la versión nueva e instálala a mano."),
            wrap_w
        );
        card_progress_->Hide();
        pnl_error_->Hide();
        btn_secondary_->Hide();
        btn_primary_->SetLabel(wxT("Cerrar esta ventana"));
        Theme::style_button_primary(btn_primary_, btn_h);
        Theme::ensure_button_min_size(btn_primary_, 190, btn_h);
        btn_primary_->Enable(true);
        Layout();
        return;
    }

    lbl_size_->SetLabel(wxString::Format(wxT("Tamaño de la descarga: %s"), format_size_mb(st.installer_size_bytes)));
    set_wrapped_static_text(
        lbl_install_note_,
        wxT("Se instalará cuando cierres el programa; Windows pedirá permiso para hacer cambios. Tus datos se conservan."),
        wrap_w
    );

    if (st.state == "DOWNLOADING") {
        if (!poll_timer_.IsRunning()) {
            poll_timer_.Start(kDialogPollIntervalMs);
        }
        const int pct = compute_download_percent(st.downloaded_bytes, st.installer_size_bytes);
        gauge_progress_->SetValue(pct);
        lbl_progress_->SetForegroundColour(Theme::ColorBrandDeep);
        lbl_progress_->SetLabel(wxString::Format(wxT("Descargando... %d %%"), pct));
        card_progress_->Show();
        pnl_error_->Hide();

        btn_secondary_->SetLabel(wxT("Cancelar"));
        Theme::style_button_secondary(btn_secondary_, btn_h);
        Theme::ensure_button_min_size(btn_secondary_, 130, btn_h);
        btn_secondary_->Show();

        btn_primary_->SetLabel(wxT("Cerrar esta ventana"));
        Theme::style_button_secondary(btn_primary_, btn_h);
        Theme::ensure_button_min_size(btn_primary_, 190, btn_h);
        btn_primary_->Show();
    } else if (st.state == "READY" && st.accepted && st.download_verified) {
        poll_timer_.Stop();
        gauge_progress_->SetValue(100);
        lbl_progress_->SetForegroundColour(Theme::ColorSuccess);
        lbl_progress_->SetLabel(wxT("Lista. Se instalará cuando cierres el programa."));
        card_progress_->Show();
        pnl_error_->Hide();

        btn_secondary_->SetLabel(wxT("No instalar todavía"));
        Theme::style_button_secondary(btn_secondary_, btn_h);
        Theme::ensure_button_min_size(btn_secondary_, 180, btn_h);
        btn_secondary_->Show();

        btn_primary_->SetLabel(wxT("Cerrar esta ventana"));
        Theme::style_button_primary(btn_primary_, btn_h);
        Theme::ensure_button_min_size(btn_primary_, 190, btn_h);
        btn_primary_->Show();
    } else {
        poll_timer_.Stop();
        card_progress_->Hide();

        const bool has_dl_error = !st.error_message.empty() &&
                                  (st.error_kind == "DOWNLOAD" || st.error_kind == "INVALID");
        if (has_dl_error) {
            const wxString err_msg = is_verification_rejection(st)
                ? wxT("La descarga no es válida y se descartó. No se instaló nada.")
                : wxString::FromUTF8(st.error_message.c_str());
            set_wrapped_static_text(lbl_error_, err_msg, wrap_w - FromDIP(24));
            pnl_error_->Show();

            btn_secondary_->SetLabel(wxT("Ahora no"));
            Theme::style_button_secondary(btn_secondary_, btn_h);
            Theme::ensure_button_min_size(btn_secondary_, 130, btn_h);
            btn_secondary_->Show();

            btn_primary_->SetLabel(wxT("Reintentar"));
            Theme::style_button_primary(btn_primary_, btn_h);
            Theme::ensure_button_min_size(btn_primary_, 160, btn_h);
            btn_primary_->Enable(!st.available_version.empty());
            btn_primary_->Show();
        } else {
            pnl_error_->Hide();

            btn_secondary_->SetLabel(wxT("Ahora no"));
            Theme::style_button_secondary(btn_secondary_, btn_h);
            Theme::ensure_button_min_size(btn_secondary_, 130, btn_h);
            btn_secondary_->Show();

            btn_primary_->SetLabel(wxT("Descargar e instalar al cerrar"));
            Theme::style_button_primary(btn_primary_, btn_h);
            Theme::ensure_button_min_size(btn_primary_, 260, btn_h);
            btn_primary_->Enable(!st.available_version.empty() && st.state == "AVAILABLE");
            btn_primary_->Show();
        }
    }

    Layout();
    Refresh();
}

void UpdateNotesDialog::on_poll_timer(wxTimerEvent&) {
    if (destroying_ || IsBeingDeleted()) {
        poll_timer_.Stop();
        return;
    }
    const std::string prev_state = last_status_.state;
    refresh_status();
    if (prev_state != last_status_.state && on_state_changed_) {
        on_state_changed_();
    }
}

void UpdateNotesDialog::on_primary_action(wxCommandEvent&) {
    if (destroying_ || !update_service_) {
        return;
    }
    if (last_status_.is_portable ||
        last_status_.state == "DOWNLOADING" ||
        (last_status_.state == "READY" && last_status_.accepted)) {
        Close();
        return;
    }

    // AVAILABLE (o reintento tras error de descarga): accept_update()
    const auto res = update_service_->accept_update();
    if (!res.is_success()) {
        set_wrapped_static_text(
            lbl_error_,
            wxString::FromUTF8(res.error_message().c_str()),
            std::max(FromDIP(260), GetClientSize().GetWidth() - FromDIP(72))
        );
        pnl_error_->Show();
        Layout();
        return;
    }
    refresh_status();
    if (on_state_changed_) {
        on_state_changed_();
    }
}

void UpdateNotesDialog::on_secondary_action(wxCommandEvent&) {
    if (destroying_ || !update_service_) {
        return;
    }
    if (last_status_.state == "DOWNLOADING") {
        update_service_->cancel_download();
        refresh_status();
        if (on_state_changed_) {
            on_state_changed_();
        }
        return;
    }
    if (last_status_.state == "READY" && last_status_.accepted) {
        update_service_->decline_update();
        refresh_status();
        if (on_state_changed_) {
            on_state_changed_();
        }
        return;
    }
    Close();
}

void UpdateNotesDialog::on_close_window(wxCloseEvent&) {
    destroying_ = true;
    poll_timer_.Stop();
    if (on_closed_) {
        on_closed_();
    }
    Destroy();
}

} // namespace app::frontend
