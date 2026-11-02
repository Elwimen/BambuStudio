#include "wgtAmsControlWebPanel.h"

#include "slic3r/GUI/DeviceWeb/DeviceWebHost.hpp"

#include <wx/sizer.h>
#include <wx/scrolwin.h>

namespace Slic3r::GUI {

namespace {
// Mod: the panel width now flexes with the (resizable) Control pane and the web
// scales its content to fit, so this is just a small floor, not the fixed width.
constexpr int kMinWidthDip  = 240;
// Initial height before the page reports. The host then follows content
// (no-preview machines drop the 44+10 preview band). 340 is the tallest stack:
// preview 44 + gap 10 + unit body 174 + down road 10 + switcher band 16
// + footer 37 + setup hint 37 + 8 page padding + rounding slack.
constexpr int kMinHeightDip = 340;
constexpr const char* kRoute = "/device_page/ams_control_web";
} // namespace

wgtAmsControlWebPanel::wgtAmsControlWebPanel(wxWindow* parent)
    : wxPanel(parent, wxID_ANY)
{
    auto* root = new wxBoxSizer(wxVERTICAL);

    SetMinSize(wxSize(FromDIP(kMinWidthDip), FromDIP(kMinHeightDip)));
    m_device_web = new DeviceWebHost(this,
                                     DeviceWebHostMode::DevicePageAmsControlWeb,
                                     kRoute,
                                     /*allow_lazy=*/true);
    m_device_web->SetMinSize(wxSize(FromDIP(kMinWidthDip), FromDIP(kMinHeightDip)));
    m_device_web->SetContentSizeChangedHandler([this](const wxSize& content_size) {
        // Mod: follow the web-reported HEIGHT only; let the WIDTH flex with the
        // Control pane (the web scales its content to fit the available width).
        SetMinSize(wxSize(-1, content_size.y));
        SetMaxSize(wxSize(-1, content_size.y));
        SetSize(wxSize(GetSize().x, content_size.y));
        Layout();
        // Mod: propagate the new height all the way up (like a page switch does),
        // incl. FitInside() on the scrolled StatusPanel — otherwise ancestors keep
        // the old allocation and the webview gets clipped (footer buttons hidden).
        for (wxWindow* w = GetParent(); w; w = w->GetParent()) {
            w->Layout();
            if (auto* sw = dynamic_cast<wxScrolledWindow*>(w))
                sw->FitInside();
            if (w->IsTopLevel())
                break;
        }
        if (auto* parent = GetParent())
            parent->SendSizeEvent();
    });

    root->Add(m_device_web, 0, wxEXPAND, 0);
    SetSizer(root);
    Layout();
}

void wgtAmsControlWebPanel::ShowAndLoad()
{
    const bool was_hidden = !IsShown();
    Show(true);
    // NavigateTo triggers EnsureBuilt on first use, and reloads after Suspend().
    // Skip while already visible so StatusPanel polling does not reload the page.
    if (m_device_web && was_hidden) {
        m_device_web->NavigateTo(kRoute);
    }
}

void wgtAmsControlWebPanel::HideAndSuspend()
{
    Show(false);
    if (m_device_web) {
        m_device_web->Suspend();
    }
}

void wgtAmsControlWebPanel::UpdateByMachine(Slic3r::MachineObject* /*obj*/)
{
    if (m_device_web && IsShownOnScreen()) {
        m_device_web->NotifyAmsControlWebChanged();
    }
}

} // namespace Slic3r::GUI
