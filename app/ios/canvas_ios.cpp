// iOS parts of the chart canvas and its option dialog: pinch zoom, drag inertia
// limits, zoom/pan limits at the poles, Retina scale of the canvas and compass,
// redraw of newly covered chart area, and the extra Chart Options toggles.
// These are members of ChartCanvas (gui/include/gui/chcanv.h) and CanvasOptions
// (gui/include/gui/canvas_options.h); they live here so that gui/src/chcanv.cpp
// and gui/src/canvas_options.cpp only carry the hooks that call them.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "gl_headers.h"  // Must come before anything using GL stuff

#include <cmath>

#include <wx/wx.h>
#include <wx/checkbox.h>

#include "model/ais_state_vars.h"
#include "model/config_vars.h"
#include "model/georef.h"
#include "model/gui_vars.h"

#include "canvas_options.h"
#include "chartdb.h"
#include "chcanv.h"
#include "compass.h"
#include "notification_manager_gui.h"
#include "ocpn_frame.h"
#include "quilt.h"

#ifdef __OCPN__IOS__

// ---------------------------------------------------------------------------
// ChartCanvas
// ---------------------------------------------------------------------------

void ChartCanvas::IosOnZoom(wxZoomGestureEvent &event) {
  // iOS: pinch zoom. Begin and end come from app/ios/pinch_zoom.cpp (ID 0x51A7).
  // Factors during the gesture can come from either source: the touch filter sends
  // them itself when it receives the touches, otherwise wxQt does (QPinchGesture).
  // Both are cumulative from the start; we zoom by the change relative to the
  // previous event, so duplicate events (same factor) do nothing and the base of the
  // factor does not matter. Each step is clamped to 0.5..2.
  // No panning: the positions of the two sources use different coordinate systems.
  static double s_lastPinchFactor = -1.0;
  static bool s_inPinch = false;
  const bool ours = event.GetId() == 0x51A7;
  if (ours && (event.IsGestureStart() || event.IsGestureEnd())) {
    s_inPinch = event.IsGestureStart();
    s_lastPinchFactor = s_inPinch ? 1.0 : -1.0;
    // Stop running motions: smooth zoom/pan, the jump animation after the own-ship
    // button and drag inertia. Otherwise they reset the scale or position or move
    // the chart away.
    StopMovement();
    if (m_animationActive) {
      m_easeTimer.Stop();
      m_animationActive = false;
    }
    m_chart_drag_inertia_timer.Stop();
    m_chart_drag_inertia_active = false;
    m_bChartDragging = false;
    m_drag_vec_x.clear();
    m_drag_vec_y.clear();
    m_drag_vec_t.clear();
    m_ocpnios_pinch_seen = true;
    return;
  }
  if (!s_inPinch || event.IsGestureStart() || event.IsGestureEnd()) return;

  double factor = event.GetZoomFactor();
  if (!std::isfinite(factor) || factor <= 0 || s_lastPinchFactor <= 0) return;
  double step = factor / s_lastPinchFactor;
  s_lastPinchFactor = factor;
  if (!std::isfinite(step) || step <= 0) return;
  step = wxMax(0.5, wxMin(2.0, step));
  if (fabs(step - 1.0) < 0.005) return;
  // Zoom directly: with "smooth zoom" ZoomCanvas lagged behind the gestures.
  DoZoomCanvas(step, false);
}

// Called from StartChartDragInertia() with the summed sample time; returns true
// when no inertia must be applied (the caller then returns).
bool ChartCanvas::IosClampDragTime(size_t n_vel, double &tacc) {
  // iOS: the timestamps are processing times, not touch times. While the main
  // thread is busy drawing, motion events arrive back to back and the velocity
  // becomes far too high (the chart flew across half the globe). Therefore count
  // no sample as shorter than one display frame (8 ms at 120 Hz), and cap the
  // velocity at 3000 points/s (IosCapDragVelocity).
  const double ocpnios_tmin = n_vel * 0.008;
  if (tacc > 0 && tacc < ocpnios_tmin) tacc = ocpnios_tmin;
  // iOS: with tacc == 0 (all samples in the same ms, or no samples, e.g. after a
  // pinch) the velocity became infinite, followed by wxRound asserts and the chart
  // at an unusable position. Never apply inertia after a pinch.
  if (!(tacc > 0) || m_ocpnios_pinch_seen) {
    m_ocpnios_pinch_seen = false;
    m_drag_vec_x.clear();
    m_drag_vec_y.clear();
    m_drag_vec_t.clear();
    return true;
  }
  return false;
}

void ChartCanvas::IosCapDragVelocity(size_t n_vel, double tacc) {
  const double vmax = 3000.0 * m_displayScale;  // pixels per second
  const double v = std::hypot(m_chart_drag_velocity_x, m_chart_drag_velocity_y);
  if (std::isfinite(v) && v > vmax) {
    m_chart_drag_velocity_x *= vmax / v;
    m_chart_drag_velocity_y *= vmax / v;
  }
  wxLogMessage("iOS: inertia n=%d t=%.3fs v=%.0f px/s (capped %.0f)",
               (int)n_vel, tacc, v, std::hypot(m_chart_drag_velocity_x,
                                                m_chart_drag_velocity_y));
}

// Called at the start of SetViewPoint(); adjusts its lat and scale_ppm arguments.
void ChartCanvas::IosClampViewPoint(double &lat, double &scale_ppm,
                                    int projection) {
  // iOS: enforce the same zoom-out limit as in OnSize/DoZoomCanvas here too.
  // The startup scale comes from opencpn.conf; a saved scale beyond the limit let
  // the chart extend past the poles on the first swipe.
  if (m_canvas_width > 0 && m_canvas_height > 0) {
    double min_ppm = wxMax(m_canvas_width, m_canvas_height) /
                     (1.2 * WGS84_semimajor_axis_meters * PI);
    if (scale_ppm < min_ppm) scale_ppm = min_ppm;
    // Also don't pan past the poles: Mercator y goes to infinity there and Qt then
    // draws the basemap as a solid fill. Clamp the center so that the top and bottom
    // edges of the screen stay within 85 degrees.
    if (projection == PROJECTION_UNKNOWN || projection == PROJECTION_MERCATOR) {
      const double a = WGS84_semimajor_axis_meters;
      const double y_lim = a * log(tan(PI / 4 + (85.0 * PI / 180.0) / 2));
      const double half = (m_canvas_height / 2.0) / scale_ppm;
      double y = a * log(tan(PI / 4 + (lat * PI / 180.0) / 2));
      const double y_max = wxMax(0.0, y_lim - half);
      if (y > y_max) y = y_max;
      if (y < -y_max) y = -y_max;
      lat = (2 * atan(exp(y / a)) - PI / 2) * 180.0 / PI;
    }
  }
}

// Called from OnSize() after m_canvas_width/height are set.
void ChartCanvas::IosOnSizeScale() {
  // iOS: see the constructor; Retina only in the GL path.
  // Also reset to 1 when OpenGL is turned off; otherwise the Retina factor stayed
  // and the raster path used 3x the pixels (zoom jump, oversized text).
  m_displayScale = g_bopengl ? GetContentScaleFactor() : 1.0;
  {
    // Recreate the embossed texts when the chart width changes (first size,
    // rotation): on iOS their size depends on the width.
    static int s_embossWidth = -1;
    if (m_canvas_width != s_embossWidth) {
      s_embossWidth = m_canvas_width;
      CreateDepthUnitEmbossMaps(m_cs);
      SetOverzoomFont();
      CreateOZEmbossMapData(m_cs);
    }
  }
}

// Called from OnPaint() after blitting the shifted cached chart and rendering the
// newly exposed update_region (locals of OnPaint passed in).
void ChartCanvas::IosRedrawNewCoverage(wxMemoryDC &temp_dc, ViewPort &svp,
                                       OCPNRegion &chart_get_region,
                                       OCPNRegion &update_region, int dx,
                                       int dy) {
  // iOS: the shifted old image is only usable if the chart coverage
  // is unchanged. If part of it was first not covered by a chart (black,
  // the world map is not in the cache) and now is, or the quilt was
  // empty then, that part stayed black or showed the old world map.
  // Redraw everything in that case.
  // An empty OCPNRegion counts as invalid (assert in Subtract),
  // so only subtract what is valid.
  m_pQuilt->ComputeRenderRegion(svp, chart_get_region);
  OCPNRegion diff = m_pQuilt->GetFullQuiltRenderedRegion();
  if (diff.IsOk() && !diff.IsEmpty()) {
    if (m_ocpnios_cache_rendered.IsOk()) {
      OCPNRegion old_r = m_ocpnios_cache_rendered;
      old_r.Offset(-dx, -dy);
      diff.Subtract(old_r);
    }
    if (update_region.IsOk()) diff.Subtract(update_region);
  }
  if (diff.IsOk() && !diff.IsEmpty()) {
    temp_dc.SelectObject(m_working_bm);
    m_pQuilt->RenderQuiltRegionViewOnDCNoText(temp_dc, svp, chart_get_region);
  }
}

// Called from UpdateGPSCompassStatusBox() before the compass is positioned.
void ChartCanvas::IosUpdateCompassScale(bool &b_update) {
  // iOS: the compass gets its size in the constructor, when the Retina factor of a
  // new chart canvas may still be 1 (5.14 recreates the canvas). The compass then
  // stayed at a third of its size, and at the top left on iPad. Set it again here
  // once the factor is known or has changed.
  if (g_bopengl && m_Compass) {
    const double cs = GetContentScaleFactor();
    if (cs != m_displayScale) {
      wxLogMessage("iOS: chart scale %.2f -> %.2f", m_displayScale, cs);
      m_displayScale = cs;
      VPoint.SetPixelScale(m_displayScale);
    }
    if (cs != m_ocpnios_compass_cs) {
      m_ocpnios_compass_cs = cs;
      m_Compass->SetScaleFactor(g_compass_scalefactor);
      // The notification bell has the same problem; UpdateStatus(true) recreates
      // its (static) background at the new size.
      if (m_notification_button) {
        m_notification_button->SetScaleFactor(g_compass_scalefactor);
        m_notification_button->UpdateStatus(true);
      }
      b_update = true;
    }
  }
}

// ---------------------------------------------------------------------------
// CanvasOptions: toggles that were only in the menu bar, which iOS removes.
// ---------------------------------------------------------------------------

void CanvasOptions::IosRefreshControlValues(ChartCanvas *parentCanvas) {
  if (m_iosNavobjects) {
    m_iosNavobjects->SetValue(parentCanvas->m_bShowNavobjects);
    m_iosChartBar->SetValue(g_bShowChartBar);
    m_iosHideMoored->SetValue(g_bHideMoored);
    m_iosAISTracks->SetValue(g_bAISShowTracks);
    m_iosCPADialog->SetValue(g_bAIS_CPA_Alert);
    m_iosCPASound->SetValue(g_bAIS_CPA_Alert_Audio);
    m_iosCPASound->Enable(g_bAIS_CPA_Alert);
    m_iosCPAWarn->SetValue(g_bCPAWarn);
  }
}

void CanvasOptions::IosUpdateCanvasOptions(ChartCanvas *parentCanvas,
                                           bool &b_needRefresh) {
  // Same behavior as the menu items (MyFrame::OnToolLeftClick), without a menu bar.
  if (m_iosNavobjects) {
    if (m_iosNavobjects->GetValue() != parentCanvas->m_bShowNavobjects)
      gFrame->ToggleNavobjects(parentCanvas);
    if (m_iosChartBar->GetValue() != g_bShowChartBar)
      gFrame->ToggleChartBar(parentCanvas);
    if (m_iosHideMoored->GetValue() != g_bHideMoored) {
      g_bHideMoored = m_iosHideMoored->GetValue();
      b_needRefresh = true;
    }
    if (m_iosAISTracks->GetValue() != g_bAISShowTracks) {
      g_bAISShowTracks = m_iosAISTracks->GetValue();
      b_needRefresh = true;
    }
    g_bAIS_CPA_Alert = m_iosCPADialog->GetValue();
    g_bAIS_CPA_Alert_Audio = m_iosCPASound->GetValue();
    if (m_iosCPAWarn->GetValue() != g_bCPAWarn) {
      // Like ChartCanvas::ToggleCPAWarn, but without a message dialog.
      g_bCPAWarn = m_iosCPAWarn->GetValue();
      g_bTCPA_Max = g_bCPAWarn;
      b_needRefresh = true;
    }
  }
}

#endif  // __OCPN__IOS__
