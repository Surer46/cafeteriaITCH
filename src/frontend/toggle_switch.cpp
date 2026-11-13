#include "src/frontend/toggle_switch.hpp"
#include "src/frontend/theme.hpp"
#include <algorithm>
#include <cmath>
#include <wx/dcbuffer.h>
#include <wx/graphics.h>

#ifdef __WXMSW__
#include <windows.h>
#endif

namespace app::frontend {

ToggleSwitch::ToggleSwitch(
    wxWindow* parent,
    wxWindowID id,
    bool initial_value,
    const wxString& accessible_label
)
    : wxControl(parent, id, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE),
      checked_(initial_value)
{
    if (!accessible_label.IsEmpty()) {
        SetLabel(accessible_label);
        SetName(accessible_label);
    }

    SetBackgroundStyle(wxBG_STYLE_PAINT);
    const wxSize best = DoGetBestSize();
    SetMinSize(best);
    SetMaxSize(best);
    SetSize(best);
    update_cursor();

    Bind(wxEVT_PAINT, &ToggleSwitch::on_paint, this);
    Bind(wxEVT_ERASE_BACKGROUND, [](wxEraseEvent&) {});
    Bind(wxEVT_LEFT_DOWN, &ToggleSwitch::on_left_down, this);
    Bind(wxEVT_LEFT_UP, &ToggleSwitch::on_left_up, this);
    Bind(wxEVT_MOUSE_CAPTURE_LOST, &ToggleSwitch::on_mouse_capture_lost, this);
    Bind(wxEVT_KEY_DOWN, &ToggleSwitch::on_key_down, this);
    Bind(wxEVT_SET_FOCUS, &ToggleSwitch::on_focus_changed, this);
    Bind(wxEVT_KILL_FOCUS, &ToggleSwitch::on_focus_changed, this);
}

wxSize ToggleSwitch::DoGetBestSize() const {
    const wxSize base = Theme::is_touch() ? wxSize(60, 32) : wxSize(44, 24);
    return FromDIP(base);
}

void ToggleSwitch::SetValue(bool value) {
    if (checked_ != value) {
        checked_ = value;
        Refresh();
    }
}

void ToggleSwitch::Toggle() {
    if (!IsEnabled()) {
        return;
    }
    checked_ = !checked_;
    Refresh();
    emit_change_event();
}

bool ToggleSwitch::Enable(bool enable) {
    const bool changed = wxControl::Enable(enable);
    update_cursor();
    if (changed) {
        Refresh();
    }
    return changed;
}

void ToggleSwitch::update_cursor() {
    SetCursor(IsEnabled() ? wxCursor(wxCURSOR_HAND) : wxNullCursor);
}

void ToggleSwitch::emit_change_event() {
    wxCommandEvent evt(wxEVT_CHECKBOX, GetId());
    evt.SetEventObject(this);
    evt.SetInt(checked_ ? 1 : 0);
    ProcessWindowEvent(evt);
}

void ToggleSwitch::on_left_down(wxMouseEvent& event) {
    if (!IsEnabled()) {
        return;
    }
    SetFocus();
    pressed_ = true;
    if (!HasCapture()) {
        CaptureMouse();
    }
}

void ToggleSwitch::on_left_up(wxMouseEvent& event) {
    const bool was_pressed = pressed_;
    pressed_ = false;
    if (HasCapture()) {
        ReleaseMouse();
    }
    if (was_pressed && IsEnabled() && wxRect(GetClientSize()).Contains(event.GetPosition())) {
        Toggle();
    }
}

void ToggleSwitch::on_mouse_capture_lost(wxMouseCaptureLostEvent&) {
    pressed_ = false;
}

void ToggleSwitch::on_key_down(wxKeyEvent& event) {
    if (event.GetKeyCode() == WXK_SPACE) {
        if (IsEnabled()) {
            Toggle();
        }
        return;
    }
    event.Skip();
}

void ToggleSwitch::on_focus_changed(wxFocusEvent& event) {
    Refresh();
    event.Skip();
}

void ToggleSwitch::on_paint(wxPaintEvent&) {
    wxAutoBufferedPaintDC dc(this);

    const wxColour parent_bg = GetParent() ? GetParent()->GetBackgroundColour() : Theme::ColorSurface;
    dc.SetBackground(wxBrush(parent_bg));
    dc.Clear();

    const wxSize sz = GetClientSize();
    if (sz.GetWidth() <= 0 || sz.GetHeight() <= 0) {
        return;
    }

    wxGraphicsContext* gc = wxGraphicsContext::Create(dc);
    if (!gc) {
        return;
    }

    const bool enabled = IsEnabled();
    wxColour track_col;
    wxColour knob_col;
    wxColour mark_col;

    if (enabled) {
        track_col = checked_ ? Theme::ColorBrand : Theme::ColorTextMuted;
        knob_col = Theme::ColorSurface;
        mark_col = Theme::ColorBrand;
    } else {
        track_col = checked_ ? wxColour(0xE2, 0xA8, 0xA8) : Theme::ColorBorderSubtle;
        knob_col = Theme::ColorSurfaceSubtle;
        mark_col = wxColour(0xD4, 0x8A, 0x8A);
    }

    const double pad = static_cast<double>(FromDIP(2));
    const double tx = pad;
    const double ty = pad;
    const double tw = static_cast<double>(sz.GetWidth()) - 2.0 * pad;
    const double th = static_cast<double>(sz.GetHeight()) - 2.0 * pad;
    const double track_radius = th / 2.0;

    if (enabled && HasFocus()) {
        const double focus_w = std::max(1.5, static_cast<double>(FromDIP(2)));
        const double fx = pad / 2.0;
        const double fy = pad / 2.0;
        const double fw = static_cast<double>(sz.GetWidth()) - pad;
        const double fh = static_cast<double>(sz.GetHeight()) - pad;
        gc->SetBrush(*wxTRANSPARENT_BRUSH);
        gc->SetPen(wxPen(Theme::ColorFocus, static_cast<int>(std::lround(focus_w))));
        gc->DrawRoundedRectangle(fx, fy, fw, fh, fh / 2.0);
    }

    gc->SetBrush(wxBrush(track_col));
    gc->SetPen(*wxTRANSPARENT_PEN);
    gc->DrawRoundedRectangle(tx, ty, tw, th, track_radius);

    const double knob_margin = std::max(2.0, th * 0.14);
    const double knob_d = th - 2.0 * knob_margin;
    const double knob_y = ty + knob_margin;
    const double knob_x = checked_ ? (tx + tw - knob_margin - knob_d) : (tx + knob_margin);

    gc->SetBrush(wxBrush(knob_col));
    gc->SetPen(*wxTRANSPARENT_PEN);
    gc->DrawEllipse(knob_x, knob_y, knob_d, knob_d);

    if (checked_) {
        const double cx = knob_x + knob_d / 2.0;
        const double cy = knob_y + knob_d / 2.0;
        const double r = knob_d * 0.28;

        wxPen mark_pen(mark_col, static_cast<int>(std::max(2L, std::lround(knob_d * 0.14))));
        mark_pen.SetCap(wxCAP_ROUND);
        mark_pen.SetJoin(wxJOIN_ROUND);
        gc->SetPen(mark_pen);

        wxGraphicsPath path = gc->CreatePath();
        path.MoveToPoint(cx - r * 0.85, cy + r * 0.05);
        path.AddLineToPoint(cx - r * 0.15, cy + r * 0.75);
        path.AddLineToPoint(cx + r * 0.90, cy - r * 0.65);
        gc->StrokePath(path);
    }

    delete gc;
}

#ifdef __WXMSW__
WXLRESULT ToggleSwitch::MSWWindowProc(WXUINT message, WXWPARAM wParam, WXLPARAM lParam) {
    switch (message) {
        case BM_GETCHECK:
            return checked_ ? BST_CHECKED : BST_UNCHECKED;
        case BM_SETCHECK: {
            const bool val = (wParam == BST_CHECKED);
            SetValue(val);
            return 0;
        }
        case BM_CLICK:
            if (IsEnabled()) {
                SetFocus();
                Toggle();
            }
            return 0;
        case WM_GETDLGCODE:
            if (wParam == VK_SPACE) {
                return DLGC_WANTMESSAGE;
            }
            break;
        default:
            break;
    }
    return wxControl::MSWWindowProc(message, wParam, lParam);
}
#endif

} // namespace app::frontend
