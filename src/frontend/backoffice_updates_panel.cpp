#include "src/frontend/backoffice_updates_panel.hpp"
#include <algorithm>
#include <wx/filedlg.h>
#include <wx/filename.h>
#include <wx/statline.h>

namespace app::frontend {

namespace {

constexpr int kPanelPollIntervalMs = 2500;

int compute_dl_pct(std::int64_t done, std::int64_t total) {
    if (total <= 0 || done <= 0) {
        return 0;
    }
    if (done >= total) {
        return 100;
    }
    return static_cast<int>((done * 100) / total);
}

void show_plain_notice(wxWindow* parent, const wxString& title, const wxString& body, bool is_error) {
    const bool touch = Theme::is_touch();
    const int btn_h = touch ? 48 : 38;
    const int wrap_w = parent->FromDIP(touch ? 560 : 460);

    wxDialog dlg(parent, wxID_ANY, title, wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE);
    dlg.SetBackgroundColour(Theme::ColorSurface);
    auto* root = new wxBoxSizer(wxVERTICAL);

    auto* lbl_head = new wxStaticText(&dlg, wxID_ANY, title);
    lbl_head->SetFont(Theme::get_font_title(wxFONTWEIGHT_BOLD));
    lbl_head->SetForegroundColour(is_error ? Theme::ColorError : Theme::ColorBrandDeep);
    lbl_head->Wrap(wrap_w);
    root->Add(lbl_head, 0, wxLEFT | wxRIGHT | wxTOP, parent->FromDIP(20));

    auto* lbl_body = new wxStaticText(&dlg, wxID_ANY, body);
    lbl_body->SetFont(Theme::get_font_body());
    lbl_body->SetForegroundColour(Theme::ColorText);
    lbl_body->Wrap(wrap_w);
    root->Add(lbl_body, 0, wxALL, parent->FromDIP(20));

    auto* btn_ok = new wxButton(&dlg, wxID_OK, wxT("Aceptar"));
    if (is_error) {
        Theme::style_button_destructive(btn_ok, btn_h);
    } else {
        Theme::style_button_primary(btn_ok, btn_h);
    }
    Theme::ensure_button_min_size(btn_ok, 140, btn_h);
    root->Add(btn_ok, 0, wxALIGN_RIGHT | wxLEFT | wxRIGHT | wxBOTTOM, parent->FromDIP(20));

    dlg.SetSizerAndFit(root);
    dlg.CentreOnParent();
    dlg.ShowModal();
}

} // namespace

BackofficeUpdatesPanel::BackofficeUpdatesPanel(
    wxWindow* parent,
    services::ServiceRegistry services,
    std::function<void()> on_open_notes,
    std::function<void()> on_state_changed
) : wxPanel(parent, wxID_ANY),
    services_(std::move(services)),
    on_open_notes_(std::move(on_open_notes)),
    on_state_changed_(std::move(on_state_changed)),
    poll_timer_(this) {
    SetBackgroundColour(Theme::ColorBackground);
    const bool has_svcs = services_.account_service && services_.update_service;
    can_manage_ = has_svcs && services_.account_service->has_permission("updates.manage");

    if (!can_manage_) {
        setup_access_denied_ui();
        return;
    }

    Bind(wxEVT_TIMER, &BackofficeUpdatesPanel::on_poll_timer, this, poll_timer_.GetId());
    setup_ui();
    refresh_status();
}

BackofficeUpdatesPanel::~BackofficeUpdatesPanel() {
    destroying_ = true;
    poll_timer_.Stop();
    DestroyChildren();
}

wxString BackofficeUpdatesPanel::format_size_mb(std::int64_t bytes) {
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

void BackofficeUpdatesPanel::setup_access_denied_ui() {
    auto* root = new wxBoxSizer(wxVERTICAL);
    auto* card = new CardPanel(this, wxID_ANY);
    card->set_card_background(Theme::ColorSurface);
    auto* cs = new wxBoxSizer(wxVERTICAL);

    auto* title = new wxStaticText(card, wxID_ANY, wxT("Sin permiso para actualizar el programa"));
    title->SetFont(Theme::get_font_title(wxFONTWEIGHT_BOLD));
    title->SetForegroundColour(Theme::ColorBrandDeep);
    cs->Add(title, 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(20));

    auto* body = new wxStaticText(
        card,
        wxID_ANY,
        wxT("Tu cuenta no tiene permiso para buscar ni aplicar actualizaciones del programa.\n\n")
        wxT("Solicita ayuda al Dueño si necesitas actualizar el sistema.")
    );
    body->SetFont(Theme::get_font_body());
    body->SetForegroundColour(Theme::ColorText);
    body->Wrap(FromDIP(640));
    cs->Add(body, 0, wxALL, FromDIP(20));

    card->SetSizer(cs);
    root->Add(card, 0, wxEXPAND | wxALL, FromDIP(12));
    root->AddStretchSpacer(1);
    SetSizer(root);
}

void BackofficeUpdatesPanel::setup_ui() {
    const bool touch = Theme::is_touch();
    const int btn_h = touch ? 48 : 38;
    const int prim_h = touch ? 52 : 42;
    const int gap = FromDIP(12);

    auto* root_sizer = new wxBoxSizer(wxVERTICAL);

    scroll_ = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL);
    scroll_->SetBackgroundColour(Theme::ColorBackground);
    scroll_->SetScrollRate(0, FromDIP(16));
    scroll_sizer_ = new wxBoxSizer(wxVERTICAL);

    // Banner inline de retroalimentación
    pnl_feedback_ = new wxPanel(scroll_, wxID_ANY);
    pnl_feedback_->SetBackgroundColour(Theme::ColorSuccessSoft);
    auto* fb_sizer = new wxBoxSizer(wxHORIZONTAL);
    lbl_feedback_ = new wxStaticText(pnl_feedback_, wxID_ANY, wxEmptyString);
    lbl_feedback_->SetFont(Theme::get_font_body(wxFONTWEIGHT_MEDIUM));
    lbl_feedback_->SetForegroundColour(Theme::ColorSuccess);
    fb_sizer->Add(lbl_feedback_, 1, wxALL, FromDIP(12));
    pnl_feedback_->SetSizer(fb_sizer);
    pnl_feedback_->Hide();
    scroll_sizer_->Add(pnl_feedback_, 0, wxEXPAND | wxBOTTOM, gap);

    // =========================================================================
    // 1. Tarjeta: Estado general y comprobación manual
    // =========================================================================
    card_status_ = new CardPanel(scroll_);
    card_status_->set_card_background(Theme::ColorSurface);
    auto* st_sizer = new wxBoxSizer(wxVERTICAL);

    auto* lbl_st_title = new wxStaticText(card_status_, wxID_ANY, wxT("Actualizaciones del programa"));
    lbl_st_title->SetFont(Theme::get_font_title(wxFONTWEIGHT_BOLD));
    lbl_st_title->SetForegroundColour(Theme::ColorBrandDeep);
    st_sizer->Add(lbl_st_title, 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(16));

    auto* grid = new wxFlexGridSizer(3, 2, FromDIP(8), FromDIP(16));
    grid->AddGrowableCol(1, 1);

    auto add_row = [&](const wxString& key, wxStaticText*& out_lbl) {
        auto* k = new wxStaticText(card_status_, wxID_ANY, key);
        k->SetFont(Theme::get_font_body(wxFONTWEIGHT_BOLD));
        k->SetForegroundColour(Theme::ColorTextSecondary);
        grid->Add(k, 0, wxALIGN_TOP);

        out_lbl = new wxStaticText(card_status_, wxID_ANY, wxEmptyString);
        out_lbl->SetFont(Theme::get_font_body(wxFONTWEIGHT_MEDIUM));
        out_lbl->SetForegroundColour(Theme::ColorText);
        grid->Add(out_lbl, 1, wxEXPAND);
    };

    add_row(wxT("Versión instalada:"), lbl_installed_ver_);
    add_row(wxT("Última comprobación:"), lbl_last_check_);
    add_row(wxT("Estado:"), lbl_check_result_);
    st_sizer->Add(grid, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(14));

    pnl_info_ = new wxPanel(card_status_, wxID_ANY);
    pnl_info_->SetBackgroundColour(Theme::ColorSurfaceSubtle);
    auto* info_sz = new wxBoxSizer(wxHORIZONTAL);
    lbl_info_ = new wxStaticText(pnl_info_, wxID_ANY, wxEmptyString);
    lbl_info_->SetFont(Theme::get_font_body());
    lbl_info_->SetForegroundColour(Theme::ColorText);
    info_sz->Add(lbl_info_, 1, wxALL, FromDIP(10));
    pnl_info_->SetSizer(info_sz);
    pnl_info_->Hide();
    st_sizer->Add(pnl_info_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(12));

    pnl_error_ = new wxPanel(card_status_, wxID_ANY);
    pnl_error_->SetBackgroundColour(Theme::ColorErrorSoft);
    auto* err_sz = new wxBoxSizer(wxHORIZONTAL);
    lbl_error_ = new wxStaticText(pnl_error_, wxID_ANY, wxEmptyString);
    lbl_error_->SetFont(Theme::get_font_body(wxFONTWEIGHT_MEDIUM));
    lbl_error_->SetForegroundColour(Theme::ColorError);
    err_sz->Add(lbl_error_, 1, wxALL, FromDIP(10));
    pnl_error_->SetSizer(err_sz);
    pnl_error_->Hide();
    st_sizer->Add(pnl_error_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(12));

    st_sizer->AddStretchSpacer(1);

    btn_check_now_ = new wxButton(card_status_, wxID_ANY, wxT("Buscar ahora"));
    Theme::style_button_primary(btn_check_now_, prim_h);
    Theme::setup_button_icon(btn_check_now_, "search", wxSize(18, 18), Theme::ColorSurface);
    Theme::ensure_button_min_size(btn_check_now_, 180, prim_h);
    btn_check_now_->Bind(wxEVT_BUTTON, &BackofficeUpdatesPanel::on_check_now, this);
    st_sizer->Add(btn_check_now_, 0, wxALL, FromDIP(16));

    card_status_->SetSizer(st_sizer);

    // =========================================================================
    // 2. Tarjeta: Versión nueva disponible / descarga / aceptada
    // =========================================================================
    card_available_ = new CardPanel(scroll_);
    card_available_->set_card_background(Theme::ColorSurface);
    auto* av_sizer = new wxBoxSizer(wxVERTICAL);

    lbl_avail_title_ = new wxStaticText(card_available_, wxID_ANY, wxT("Versión nueva disponible"));
    lbl_avail_title_->SetFont(Theme::get_font_title(wxFONTWEIGHT_BOLD));
    lbl_avail_title_->SetForegroundColour(Theme::ColorBrandDeep);
    av_sizer->Add(lbl_avail_title_, 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(16));

    lbl_avail_summary_ = new wxStaticText(card_available_, wxID_ANY, wxEmptyString);
    lbl_avail_summary_->SetFont(Theme::get_font_body());
    lbl_avail_summary_->SetForegroundColour(Theme::ColorText);
    av_sizer->Add(lbl_avail_summary_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(10));

    gauge_dl_ = new wxGauge(card_available_, wxID_ANY, 100, wxDefaultPosition, wxSize(-1, FromDIP(16)));
    gauge_dl_->Hide();
    av_sizer->Add(gauge_dl_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(12));

    pnl_dl_error_ = new wxPanel(card_available_, wxID_ANY);
    pnl_dl_error_->SetBackgroundColour(Theme::ColorErrorSoft);
    auto* dl_err_sz = new wxBoxSizer(wxHORIZONTAL);
    lbl_dl_error_ = new wxStaticText(pnl_dl_error_, wxID_ANY, wxEmptyString);
    lbl_dl_error_->SetFont(Theme::get_font_body(wxFONTWEIGHT_MEDIUM));
    lbl_dl_error_->SetForegroundColour(Theme::ColorError);
    dl_err_sz->Add(lbl_dl_error_, 1, wxALL, FromDIP(10));
    pnl_dl_error_->SetSizer(dl_err_sz);
    pnl_dl_error_->Hide();
    av_sizer->Add(pnl_dl_error_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(12));

    auto* av_btn_row = new wxBoxSizer(wxHORIZONTAL);
    btn_view_notes_ = new wxButton(card_available_, wxID_ANY, wxT("Ver novedades"));
    Theme::style_button_secondary(btn_view_notes_, btn_h);
    Theme::ensure_button_min_size(btn_view_notes_, 160, btn_h);
    btn_view_notes_->Bind(wxEVT_BUTTON, &BackofficeUpdatesPanel::on_open_notes, this);
    av_btn_row->Add(btn_view_notes_, 0, wxRIGHT, FromDIP(12));

    btn_avail_action_ = new wxButton(card_available_, wxID_ANY, wxT("Descargar e instalar al cerrar"));
    Theme::style_button_primary(btn_avail_action_, btn_h);
    Theme::ensure_button_min_size(btn_avail_action_, 240, btn_h);
    btn_avail_action_->Bind(wxEVT_BUTTON, &BackofficeUpdatesPanel::on_accept_or_cancel, this);
    av_btn_row->Add(btn_avail_action_, 0);
    av_sizer->Add(av_btn_row, 0, wxALL, FromDIP(16));

    card_available_->SetSizer(av_sizer);
    card_available_->Hide();
    scroll_sizer_->Add(card_available_, 0, wxEXPAND | wxBOTTOM, gap);

    // =========================================================================
    // 3. Tarjeta: Actualizar desde un archivo (USB)
    // =========================================================================
    card_usb_ = new CardPanel(scroll_);
    card_usb_->set_card_background(Theme::ColorSurface);
    auto* usb_sizer = new wxBoxSizer(wxVERTICAL);

    auto* lbl_usb_title = new wxStaticText(card_usb_, wxID_ANY, wxT("Actualizar desde un archivo (USB)"));
    lbl_usb_title->SetFont(Theme::get_font_title(wxFONTWEIGHT_BOLD));
    lbl_usb_title->SetForegroundColour(Theme::ColorBrandDeep);
    usb_sizer->Add(lbl_usb_title, 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(16));

    lbl_usb_desc_ = new wxStaticText(
        card_usb_,
        wxID_ANY,
        wxT("Si descargaste una versión nueva en otra computadora y la copiaste en una memoria USB, ")
        wxT("puedes elegir los archivos aquí para dejarla lista e instalarla al cerrar el programa.")
    );
    lbl_usb_desc_->SetFont(Theme::get_font_body());
    lbl_usb_desc_->SetForegroundColour(Theme::ColorTextSecondary);
    lbl_usb_desc_->Wrap(FromDIP(640));
    usb_sizer->Add(lbl_usb_desc_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(10));
    usb_sizer->AddStretchSpacer(1);

    btn_usb_update_ = new wxButton(card_usb_, wxID_ANY, wxT("Actualizar desde un archivo (USB)"));
    Theme::style_button_secondary(btn_usb_update_, prim_h);
    Theme::setup_button_icon(btn_usb_update_, "inventory_2", wxSize(18, 18), Theme::ColorTextSecondary);
    Theme::ensure_button_min_size(btn_usb_update_, 280, prim_h);
    btn_usb_update_->Bind(wxEVT_BUTTON, &BackofficeUpdatesPanel::on_usb_update, this);
    usb_sizer->Add(btn_usb_update_, 0, wxALL, FromDIP(16));

    card_usb_->SetSizer(usb_sizer);

    cards_row_sizer_ = new wxBoxSizer(wxVERTICAL);
    cards_row_sizer_->Add(card_status_, 0, wxEXPAND | wxBOTTOM, gap);
    cards_row_sizer_->Add(card_usb_, 0, wxEXPAND | wxBOTTOM, FromDIP(4));
    scroll_sizer_->Add(cards_row_sizer_, 0, wxEXPAND);

    scroll_->SetSizer(scroll_sizer_);
    scroll_->Bind(wxEVT_SIZE, [this](wxSizeEvent& ev) {
        ev.Skip();
        update_scroll_layout();
    });

    root_sizer->Add(scroll_, 1, wxEXPAND);
    SetSizer(root_sizer);
}

void BackofficeUpdatesPanel::update_scroll_layout() {
    if (destroying_ || IsBeingDeleted() || !scroll_ || !scroll_sizer_) {
        return;
    }
    const wxSize client = scroll_->GetClientSize();
    const int avail_w = client.GetWidth();
    const int gap = FromDIP(12);
    const bool want_wide = (avail_w >= FromDIP(1100));

    if (cards_row_sizer_ && (!layout_initialized_ || want_wide != wide_layout_)) {
        layout_initialized_ = true;
        wide_layout_ = want_wide;
        cards_row_sizer_->Detach(card_status_);
        cards_row_sizer_->Detach(card_usb_);
        cards_row_sizer_->SetOrientation(wide_layout_ ? wxHORIZONTAL : wxVERTICAL);
        if (wide_layout_) {
            cards_row_sizer_->Add(card_status_, 1, wxEXPAND | wxRIGHT, gap);
            cards_row_sizer_->Add(card_usb_, 1, wxEXPAND);
        } else {
            cards_row_sizer_->Add(card_status_, 0, wxEXPAND | wxBOTTOM, gap);
            cards_row_sizer_->Add(card_usb_, 0, wxEXPAND | wxBOTTOM, FromDIP(4));
        }
        if (auto* row_item = scroll_sizer_->GetItem(cards_row_sizer_)) {
            row_item->SetProportion(wide_layout_ ? 1 : 0);
        }
    }

    if (avail_w > FromDIP(240)) {
        const int wrap_w = std::max(FromDIP(220), avail_w - FromDIP(64));
        const int card_wrap_w = wide_layout_
            ? std::max(FromDIP(220), (avail_w - gap) / 2 - FromDIP(48))
            : wrap_w;
        if (lbl_check_result_) {
            lbl_check_result_->Wrap(std::max(FromDIP(160), card_wrap_w - FromDIP(180)));
        }
        if (lbl_info_ && pnl_info_ && pnl_info_->IsShown()) {
            lbl_info_->Wrap(std::max(FromDIP(160), card_wrap_w - FromDIP(24)));
        }
        if (lbl_error_ && pnl_error_ && pnl_error_->IsShown()) {
            lbl_error_->Wrap(std::max(FromDIP(160), card_wrap_w - FromDIP(24)));
        }
        if (lbl_avail_summary_) {
            lbl_avail_summary_->Wrap(wrap_w);
        }
        if (lbl_dl_error_ && pnl_dl_error_ && pnl_dl_error_->IsShown()) {
            lbl_dl_error_->Wrap(wrap_w - FromDIP(24));
        }
        if (lbl_usb_desc_) {
            lbl_usb_desc_->SetLabel(
                wxT("Si descargaste una versión nueva en otra computadora y la copiaste en una memoria USB, ")
                wxT("puedes elegir los archivos aquí para dejarla lista e instalarla al cerrar el programa.")
            );
            lbl_usb_desc_->Wrap(card_wrap_w);
        }
        if (lbl_feedback_ && pnl_feedback_ && pnl_feedback_->IsShown()) {
            lbl_feedback_->Wrap(wrap_w);
        }
        for (CardPanel* cp : {card_status_, card_available_, card_usb_}) {
            if (cp && cp->IsShown()) {
                cp->InvalidateBestSize();
                cp->Layout();
            }
        }
    }

    const wxSize min_sz = scroll_sizer_->GetMinSize();
    const int virt_w = std::max(client.GetWidth(), 0);
    const int virt_h = std::max(client.GetHeight(), min_sz.GetHeight());
    scroll_->SetVirtualSize(virt_w, wide_layout_ ? virt_h : min_sz.GetHeight());
    scroll_sizer_->SetDimension(scroll_->CalcScrolledPosition(wxPoint(0, 0)), wxSize(virt_w, virt_h));
    scroll_sizer_->Layout();
}

void BackofficeUpdatesPanel::show_feedback(FeedbackKind kind, const wxString& message) {
    if (destroying_ || !pnl_feedback_ || !lbl_feedback_) {
        return;
    }
    if (kind == FeedbackKind::Success) {
        pnl_feedback_->SetBackgroundColour(Theme::ColorSuccessSoft);
        lbl_feedback_->SetForegroundColour(Theme::ColorSuccess);
    } else if (kind == FeedbackKind::Error) {
        pnl_feedback_->SetBackgroundColour(Theme::ColorErrorSoft);
        lbl_feedback_->SetForegroundColour(Theme::ColorError);
    } else {
        pnl_feedback_->SetBackgroundColour(Theme::ColorSurfaceSubtle);
        lbl_feedback_->SetForegroundColour(Theme::ColorText);
    }
    lbl_feedback_->SetLabel(message);
    pnl_feedback_->Show();
    pnl_feedback_->Layout();
    update_scroll_layout();
}

void BackofficeUpdatesPanel::hide_feedback() {
    if (pnl_feedback_) {
        pnl_feedback_->Hide();
        update_scroll_layout();
    }
}

void BackofficeUpdatesPanel::refresh_status() {
    if (destroying_ || IsBeingDeleted() || !can_manage_ || !services_.update_service) {
        poll_timer_.Stop();
        return;
    }
    const auto res = services_.update_service->get_update_status();
    if (!res.is_success()) {
        poll_timer_.Stop();
        show_feedback(FeedbackKind::Error, wxString::FromUTF8(res.error_message().c_str()));
        return;
    }
    last_status_ = res.value();
    apply_status(last_status_);

    if (last_status_.state == "CHECKING" || last_status_.state == "DOWNLOADING") {
        if (!poll_timer_.IsRunning()) {
            poll_timer_.Start(kPanelPollIntervalMs);
        }
    } else {
        poll_timer_.Stop();
    }
}

void BackofficeUpdatesPanel::apply_status(const contracts::UpdateStatusDto& st) {
    if (destroying_ || IsBeingDeleted()) {
        return;
    }
    const bool touch = Theme::is_touch();
    const int btn_h = touch ? 48 : 38;

    lbl_installed_ver_->SetLabel(wxString::FromUTF8(st.installed_version.c_str()));
    lbl_last_check_->SetLabel(
        st.last_check_at.empty() ? wxT("Nunca") : wxString::FromUTF8(st.last_check_at.c_str())
    );

    const wxString avail_ver = wxString::FromUTF8(st.available_version.c_str());

    if (st.state == "NOT_CONFIGURED") {
        lbl_check_result_->SetLabel(wxT("Las actualizaciones por internet todavía no están activadas en este programa."));
        lbl_check_result_->SetForegroundColour(Theme::ColorTextSecondary);
    } else if (st.state == "CHECKING") {
        lbl_check_result_->SetLabel(wxT("Buscando actualizaciones..."));
        lbl_check_result_->SetForegroundColour(Theme::ColorFocus);
    } else if (st.state == "DOWNLOADING") {
        const int pct = compute_dl_pct(st.downloaded_bytes, st.installer_size_bytes);
        lbl_check_result_->SetLabel(wxString::Format(wxT("Descargando versión %s (%d %%)..."), avail_ver, pct));
        lbl_check_result_->SetForegroundColour(Theme::ColorFocus);
    } else if (st.state == "READY" && st.accepted) {
        lbl_check_result_->SetLabel(
            wxString::Format(wxT("Lista: la versión %s se instalará al cerrar el programa."), avail_ver)
        );
        lbl_check_result_->SetForegroundColour(Theme::ColorSuccess);
    } else if (st.state == "AVAILABLE") {
        lbl_check_result_->SetLabel(wxString::Format(wxT("Hay una versión nueva (%s)."), avail_ver));
        lbl_check_result_->SetForegroundColour(Theme::ColorBrandDeep);
    } else if (st.state == "ERROR") {
        if (st.error_kind == "NO_NETWORK") {
            lbl_check_result_->SetLabel(wxT("No se pudo comprobar: sin conexión."));
        } else {
            lbl_check_result_->SetLabel(wxT("No se pudo comprobar."));
        }
        lbl_check_result_->SetForegroundColour(Theme::ColorTextSecondary);
    } else {
        lbl_check_result_->SetLabel(wxT("Tienes la versión más reciente."));
        lbl_check_result_->SetForegroundColour(Theme::ColorSuccess);
    }

    // Aviso informativo (portable o espera entre búsquedas manuales — nunca tratado como error)
    if (st.is_portable) {
        lbl_info_->SetLabel(
            wxT("Esta copia es portable: no se actualiza sola. Descarga la versión nueva e instálala a mano.")
        );
        pnl_info_->Show();
    } else if (st.manual_check_throttled && !st.next_manual_check_at.empty()) {
        const wxString info_wx = st.info_message.empty()
            ? wxString::Format(wxT("Podrás buscar de nuevo a partir de las %s."), wxString::FromUTF8(st.next_manual_check_at.c_str()))
            : wxString::FromUTF8(st.info_message.c_str());
        lbl_info_->SetLabel(info_wx);
        pnl_info_->Show();
    } else {
        pnl_info_->Hide();
    }

    // Detalle de error de comprobación: mostrarlo solo si vino de búsqueda manual
    if (st.state == "ERROR" && st.error_from_manual_check && !st.error_message.empty()) {
        lbl_error_->SetLabel(wxString::FromUTF8(st.error_message.c_str()));
        pnl_error_->Show();
    } else {
        pnl_error_->Hide();
    }

    // Botón Buscar ahora: oculto en NOT_CONFIGURED y portable; deshabilitado mientras busca o descarga o está en pausa
    const bool show_check_btn = !st.is_portable && st.state != "NOT_CONFIGURED";
    btn_check_now_->Show(show_check_btn);
    if (show_check_btn) {
        const bool can_click_check = (st.state != "CHECKING" && st.state != "DOWNLOADING" && !st.manual_check_throttled);
        btn_check_now_->Enable(can_click_check);
    }

    // Tarjeta de versión disponible / descarga / lista (oculta en portable porque no descarga ni instala)
    const bool show_avail_card = !st.is_portable &&
                                 (st.state == "AVAILABLE" || st.state == "DOWNLOADING" || st.state == "READY");
    card_available_->Show(show_avail_card);
    if (show_avail_card) {
        if (st.state == "READY" && st.accepted && st.download_verified) {
            lbl_avail_title_->SetLabel(wxString::Format(wxT("Versión %s lista para instalar"), avail_ver));
            lbl_avail_summary_->SetLabel(
                wxT("La descarga está completa y verificada. Se instalará al cerrar el programa.")
            );
            gauge_dl_->Hide();
            pnl_dl_error_->Hide();
            btn_avail_action_->SetLabel(wxT("No instalar todavía"));
            Theme::style_button_secondary(btn_avail_action_, btn_h);
            Theme::ensure_button_min_size(btn_avail_action_, 190, btn_h);
        } else if (st.state == "DOWNLOADING") {
            const int pct = compute_dl_pct(st.downloaded_bytes, st.installer_size_bytes);
            lbl_avail_title_->SetLabel(wxString::Format(wxT("Descargando versión %s"), avail_ver));
            lbl_avail_summary_->SetLabel(
                wxString::Format(wxT("Descargando... %d %% (%s). Puedes seguir trabajando."), pct, format_size_mb(st.installer_size_bytes))
            );
            gauge_dl_->SetValue(pct);
            gauge_dl_->Show();
            pnl_dl_error_->Hide();
            btn_avail_action_->SetLabel(wxT("Cancelar"));
            Theme::style_button_secondary(btn_avail_action_, btn_h);
            Theme::ensure_button_min_size(btn_avail_action_, 140, btn_h);
        } else {
            lbl_avail_title_->SetLabel(wxString::Format(wxT("Versión nueva %s"), avail_ver));
            lbl_avail_summary_->SetLabel(
                wxString::Format(wxT("Tamaño de la descarga: %s. Puedes revisar qué cambia antes de aceptarla."),
                                 format_size_mb(st.installer_size_bytes))
            );
            gauge_dl_->Hide();
            if (!st.error_message.empty() && (st.error_kind == "DOWNLOAD" || st.error_kind == "INVALID")) {
                const wxString err_dl = (st.download_error_kind == "INVALID")
                    ? wxT("La descarga no es válida y se descartó. No se instaló nada.")
                    : wxString::FromUTF8(st.error_message.c_str());
                lbl_dl_error_->SetLabel(err_dl);
                pnl_dl_error_->Show();
            } else {
                pnl_dl_error_->Hide();
            }
            btn_avail_action_->SetLabel(wxT("Descargar e instalar al cerrar"));
            Theme::style_button_primary(btn_avail_action_, btn_h);
            Theme::ensure_button_min_size(btn_avail_action_, 260, btn_h);
        }
    }

    // Tarjeta USB: oculta en copias portables (solo aviso)
    card_usb_->Show(!st.is_portable);
    if (btn_usb_update_) {
        btn_usb_update_->Enable(st.state != "CHECKING" && st.state != "DOWNLOADING");
    }

    card_status_->Layout();
    if (card_available_->IsShown()) {
        card_available_->Layout();
    }
    if (card_usb_->IsShown()) {
        card_usb_->Layout();
    }
    update_scroll_layout();
}

void BackofficeUpdatesPanel::on_poll_timer(wxTimerEvent&) {
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

void BackofficeUpdatesPanel::on_check_now(wxCommandEvent&) {
    if (destroying_ || !services_.update_service) {
        return;
    }
    hide_feedback();
    const auto res = services_.update_service->check_for_updates();
    if (!res.is_success()) {
        show_feedback(FeedbackKind::Error, wxString::FromUTF8(res.error_message().c_str()));
        return;
    }
    refresh_status();
    if (on_state_changed_) {
        on_state_changed_();
    }
}

void BackofficeUpdatesPanel::on_open_notes(wxCommandEvent&) {
    if (destroying_ || !on_open_notes_) {
        return;
    }
    on_open_notes_();
}

void BackofficeUpdatesPanel::on_accept_or_cancel(wxCommandEvent&) {
    if (destroying_ || !services_.update_service) {
        return;
    }
    hide_feedback();
    contracts::OperationResult res = contracts::OperationResult::ok();
    if (last_status_.state == "DOWNLOADING") {
        res = services_.update_service->cancel_download();
    } else if (last_status_.state == "READY" && last_status_.accepted) {
        res = services_.update_service->decline_update();
    } else {
        res = services_.update_service->accept_update();
    }

    if (!res.is_success()) {
        show_feedback(FeedbackKind::Error, wxString::FromUTF8(res.error_message().c_str()));
    }
    refresh_status();
    if (on_state_changed_) {
        on_state_changed_();
    }
}

void BackofficeUpdatesPanel::on_usb_update(wxCommandEvent&) {
    if (destroying_ || !services_.update_service) {
        return;
    }
    hide_feedback();

    wxFileDialog exe_dlg(
        this,
        wxT("Elegir instalador de actualización"),
        wxEmptyString,
        wxEmptyString,
        wxT("Instalador del programa (*.exe)|*.exe"),
        wxFD_OPEN | wxFD_FILE_MUST_EXIST
    );
    if (exe_dlg.ShowModal() != wxID_OK) {
        return;
    }
    const wxString exe_path = exe_dlg.GetPath();
    const wxFileName exe_fn(exe_path);
    const wxString base_dir = exe_fn.GetPath();

    wxString default_data;
    const wxFileName cand_data(base_dir, wxT("manifiesto.json"));
    if (cand_data.FileExists()) {
        default_data = cand_data.GetFullName();
    }

    wxFileDialog data_dlg(
        this,
        wxT("Elegir archivo de datos de la actualización"),
        base_dir,
        default_data,
        wxT("Archivo de datos (*.json)|*.json|Todos los archivos (*.*)|*.*"),
        wxFD_OPEN | wxFD_FILE_MUST_EXIST
    );
    if (data_dlg.ShowModal() != wxID_OK) {
        return;
    }
    const wxString data_path = data_dlg.GetPath();

    wxString default_sig;
    const wxFileName cand_sig(base_dir, wxT("manifiesto.json.sig"));
    if (cand_sig.FileExists()) {
        default_sig = cand_sig.GetFullName();
    }

    wxFileDialog sig_dlg(
        this,
        wxT("Elegir archivo de verificación de la actualización"),
        base_dir,
        default_sig,
        wxT("Archivo de verificación (*.sig)|*.sig|Todos los archivos (*.*)|*.*"),
        wxFD_OPEN | wxFD_FILE_MUST_EXIST
    );
    if (sig_dlg.ShowModal() != wxID_OK) {
        return;
    }
    const wxString sig_path = sig_dlg.GetPath();

    const auto res = services_.update_service->apply_update_from_files(
        std::string(exe_path.ToUTF8().data()),
        std::string(data_path.ToUTF8().data()),
        std::string(sig_path.ToUTF8().data())
    );
    if (!res.is_success()) {
        const wxString err_wx = wxString::FromUTF8(res.error_message().c_str());
        show_feedback(FeedbackKind::Error, err_wx);
        show_plain_notice(this, wxT("No se pudo usar la actualización"), err_wx, true);
        refresh_status();
        if (on_state_changed_) {
            on_state_changed_();
        }
        return;
    }

    const auto& usb_res = res.value();
    if (!usb_res.accepted) {
        const wxString err_wx = (usb_res.reason == "NOT_NEWER")
            ? wxT("Esa versión no es más nueva que la instalada.")
            : wxString::FromUTF8(usb_res.message.c_str());
        show_feedback(FeedbackKind::Error, err_wx);
        show_plain_notice(this, wxT("No se pudo usar la actualización"), err_wx, true);
        refresh_status();
        if (on_state_changed_) {
            on_state_changed_();
        }
        return;
    }

    refresh_status();
    if (on_state_changed_) {
        on_state_changed_();
    }
    const wxString ok_msg = wxString::Format(
        wxT("La actualización (versión %s) se verificó y quedó lista.\n\nSe instalará cuando cierres el programa."),
        wxString::FromUTF8(last_status_.available_version.c_str())
    );
    show_feedback(FeedbackKind::Success, wxT("Actualización lista para instalar al cerrar el programa."));
    show_plain_notice(this, wxT("Actualización lista"), ok_msg, false);
}

} // namespace app::frontend
