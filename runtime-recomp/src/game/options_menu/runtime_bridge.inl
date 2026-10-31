// Included into runtime_ui.cpp: the sound and presentation settings the
// redesigned in-game Options menu shares with the launcher (runtime_ui.hpp).
// Reuses the launcher's own helpers so both surfaces stay in step.

float dkr::runtime::ui::sound_level(std::size_t channel) {
    return channel < kSoundChannelCount ? SoundLevel(channel) : 0.0F;
}

void dkr::runtime::ui::set_sound_level(std::size_t channel, float level) {
    if (channel < kSoundChannelCount) SetSoundLevel(channel, std::clamp(level, 0.0F, 1.0F));
}

std::vector<std::string> dkr::runtime::ui::sound_preset_labels() {
    std::vector<std::string> labels;
    for (const SoundPreset& preset : kSoundPresets) labels.emplace_back(preset.label);
    return labels;
}

std::optional<std::size_t> dkr::runtime::ui::sound_preset() {
    const SoundPreset* preset = MatchSoundPreset(SoundCurve());
    if (preset == nullptr) return std::nullopt;
    return static_cast<std::size_t>(preset - kSoundPresets.data());
}

bool dkr::runtime::ui::sound_custom_curve_kept() {
    return g_eq_custom.has_value() && MatchSoundPreset(*g_eq_custom) == nullptr;
}

void dkr::runtime::ui::apply_sound_preset(std::optional<std::size_t> preset) {
    if (!preset) {
        if (g_eq_custom) SetSoundCurve(*g_eq_custom);
    } else if (*preset < kSoundPresets.size()) {
        SetSoundCurve(kSoundPresets[*preset].eq);
    }
}

int dkr::runtime::ui::sound_band(std::size_t band) {
    return band < 3U ? SoundCurve()[band] : 0;
}

void dkr::runtime::ui::set_sound_band(std::size_t band, int decibels) {
    if (band >= 3U) return;
    std::array<int, 3> eq = SoundCurve();
    eq[band] = std::clamp(decibels, -12, 12);
    SetSoundCurve(eq);
    if (MatchSoundPreset(eq) == nullptr) g_eq_custom = eq;
}

void dkr::runtime::ui::set_presentation_profile(enhancements::PresentationProfile profile) {
    namespace enh = dkr::runtime::enhancements;
    if (profile == enh::presentation_profile()) return;
    g_track_lab_modern_notice.clear();
    GraphicsConfig config = ultramodern::renderer::get_graphics_config();
    if (enh::presentation_profile() == enh::PresentationProfile::Modern) RememberModernGraphics(config);
    enh::set_presentation_profile(profile);
    ApplyProfileGraphics(config, profile);
    ultramodern::renderer::set_graphics_config(config);
    SaveSettings();
}
