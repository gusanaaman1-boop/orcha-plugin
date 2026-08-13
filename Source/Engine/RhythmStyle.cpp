#include "RhythmStyle.h"

namespace orcha
{

// DUM -> LOW, TAK -> HIGH, KA -> ghost HIGH/MID. The Arabic skeletons are
// *inspired by* the named iqa'at, quantized to a 16-step bar; they are
// protected so low randomness keeps their identity intact.
static StyleInfo makeEdm()
{
    StyleInfo s;
    s.fourFloorAnchor = true;
    s.ornamentDensity = 0.45f;
    s.skeletons = {
        { "four_floor", {
            { 0, Role::LOW, 1.0f }, { 4, Role::LOW, 0.9f }, { 8, Role::LOW, 1.0f }, { 12, Role::LOW, 0.9f },
            { 4, Role::MID, 0.8f }, { 12, Role::MID, 0.85f },
            { 2, Role::HIGH, 0.5f }, { 6, Role::HIGH, 0.5f }, { 10, Role::HIGH, 0.5f }, { 14, Role::HIGH, 0.55f } },
          0.0, { 3, 7, 11, 15 } },
        { "afro_house", {
            { 0, Role::LOW, 1.0f }, { 4, Role::LOW, 0.85f }, { 8, Role::LOW, 1.0f }, { 12, Role::LOW, 0.85f },
            { 3, Role::MID, 0.6f }, { 7, Role::MID, 0.7f }, { 11, Role::MID, 0.6f },
            { 2, Role::HIGH, 0.5f }, { 6, Role::HIGH, 0.5f }, { 10, Role::HIGH, 0.5f }, { 14, Role::HIGH, 0.5f } },
          0.18, { 5, 9, 13, 15 } },
        { "melodic_techno", {
            { 0, Role::LOW, 1.0f }, { 4, Role::LOW, 0.9f }, { 8, Role::LOW, 1.0f }, { 12, Role::LOW, 0.9f },
            { 6, Role::MID, 0.65f }, { 14, Role::MID, 0.7f },
            { 2, Role::HIGH, 0.45f }, { 10, Role::HIGH, 0.45f } },
          0.05, { 3, 7, 11, 13, 15 } },
        // Garage-leaning shuffle: floor intact, snare answers pushed off-grid.
        { "garage_shuffle", {
            { 0, Role::LOW, 1.0f }, { 4, Role::LOW, 0.85f }, { 8, Role::LOW, 1.0f }, { 12, Role::LOW, 0.85f },
            { 4, Role::MID, 0.8f }, { 12, Role::MID, 0.8f }, { 7, Role::MID, 0.5f },
            { 2, Role::HIGH, 0.55f }, { 6, Role::HIGH, 0.5f }, { 10, Role::HIGH, 0.55f }, { 15, Role::HIGH, 0.45f } },
          0.3, { 3, 9, 11, 13 } },
        // Hard techno: relentless floor, offbeat HIGH stabs, no snare at all.
        { "hard_techno", {
            { 0, Role::LOW, 1.0f }, { 4, Role::LOW, 1.0f }, { 8, Role::LOW, 1.0f }, { 12, Role::LOW, 1.0f },
            { 2, Role::HIGH, 0.7f }, { 6, Role::HIGH, 0.7f }, { 10, Role::HIGH, 0.7f }, { 14, Role::HIGH, 0.7f } },
          0.0, { 3, 7, 11, 15 } } };
    return s;
}

static StyleInfo makeArabic()
{
    StyleInfo s;
    s.ornamentDensity = 0.55f;
    s.skeletons = {
        // Maqsum: D T - T | D - T -
        { "maqsum", {
            { 0, Role::LOW, 1.0f }, { 2, Role::HIGH, 0.8f }, { 6, Role::HIGH, 0.75f },
            { 8, Role::LOW, 0.95f }, { 12, Role::HIGH, 0.8f } },
          0.1, { 4, 10, 14, 15 } },
        // Baladi: D D - T | D - T -
        { "baladi", {
            { 0, Role::LOW, 1.0f }, { 2, Role::LOW, 0.9f }, { 6, Role::HIGH, 0.75f },
            { 8, Role::LOW, 0.95f }, { 12, Role::HIGH, 0.8f } },
          0.1, { 4, 10, 14, 15 } },
        // Sa'idi: D T - D | D - T -
        { "saidi", {
            { 0, Role::LOW, 1.0f }, { 2, Role::HIGH, 0.75f }, { 6, Role::LOW, 0.95f },
            { 8, Role::LOW, 1.0f }, { 12, Role::HIGH, 0.8f } },
          0.1, { 4, 10, 14, 15 } },
        // Malfuf: driving 2/4, doubled to fill the bar: D - - T - - T -
        { "malfuf", {
            { 0, Role::LOW, 1.0f }, { 3, Role::HIGH, 0.7f }, { 6, Role::HIGH, 0.7f },
            { 8, Role::LOW, 0.95f }, { 11, Role::HIGH, 0.7f }, { 14, Role::HIGH, 0.75f } },
          0.0, { 2, 5, 10, 13 } },
        // Ayyub: hypnotic 2/4, doubled: D - - D T - is approximated per half-bar.
        { "ayyub", {
            { 0, Role::LOW, 1.0f }, { 3, Role::LOW, 0.8f }, { 6, Role::HIGH, 0.7f },
            { 8, Role::LOW, 1.0f }, { 11, Role::LOW, 0.8f }, { 14, Role::HIGH, 0.7f } },
          0.12, { 2, 5, 10, 13 } },
        // Ciftetelli: slow ornamental frame.
        { "ciftetelli", {
            { 0, Role::LOW, 1.0f }, { 4, Role::HIGH, 0.6f }, { 6, Role::HIGH, 0.6f },
            { 8, Role::LOW, 0.85f }, { 10, Role::LOW, 0.8f }, { 12, Role::HIGH, 0.7f } },
          0.15, { 2, 3, 14, 15 } },
        // Wahda: the sparse "one" - a single DUM holding the bar, TAK answers.
        { "wahda", {
            { 0, Role::LOW, 1.0f }, { 6, Role::HIGH, 0.7f }, { 8, Role::HIGH, 0.65f },
            { 12, Role::HIGH, 0.75f } },
          0.12, { 3, 5, 10, 14, 15 } },
        // Masmudi-inspired: two heavy DUMs opening the phrase.
        { "masmudi", {
            { 0, Role::LOW, 1.0f }, { 2, Role::LOW, 0.95f }, { 6, Role::HIGH, 0.7f },
            { 8, Role::LOW, 0.9f }, { 11, Role::HIGH, 0.7f }, { 12, Role::HIGH, 0.75f }, { 14, Role::HIGH, 0.6f } },
          0.1, { 4, 5, 10, 15 } } };
    return s;
}

static StyleInfo makeMediterranean()
{
    StyleInfo s;
    s.ornamentDensity = 0.6f;
    s.skeletons = {
        // Maqsum frame over a dance-floor pulse; answers live in the back half.
        { "med_maqsum_drive", {
            { 0, Role::LOW, 1.0f }, { 2, Role::HIGH, 0.75f }, { 6, Role::HIGH, 0.7f },
            { 8, Role::LOW, 0.95f }, { 10, Role::MID, 0.6f }, { 12, Role::HIGH, 0.8f } },
          0.12, { 4, 5, 13, 14, 15 } },
        { "med_call_response", {
            { 0, Role::LOW, 1.0f }, { 3, Role::MID, 0.7f }, { 4, Role::LOW, 0.8f },
            { 8, Role::LOW, 0.95f }, { 11, Role::HIGH, 0.75f }, { 14, Role::HIGH, 0.7f } },
          0.15, { 2, 6, 10, 13, 15 } },
        { "med_halftime", {
            { 0, Role::LOW, 1.0f }, { 6, Role::HIGH, 0.7f }, { 8, Role::MID, 0.8f },
            { 12, Role::HIGH, 0.75f }, { 14, Role::MID, 0.55f } },
          0.1, { 2, 3, 10, 11, 15 } },
        // Dabke-leaning line: stomping LOW pair, answers pushed to the "and".
        { "med_dabke_stomp", {
            { 0, Role::LOW, 1.0f }, { 2, Role::LOW, 0.85f }, { 6, Role::MID, 0.75f },
            { 8, Role::LOW, 0.95f }, { 10, Role::LOW, 0.7f }, { 14, Role::HIGH, 0.75f } },
          0.08, { 4, 7, 12, 13, 15 } },
        // Malfuf under a four-floor half: the festival crossover groove.
        { "med_malfuf_pulse", {
            { 0, Role::LOW, 1.0f }, { 3, Role::HIGH, 0.75f }, { 6, Role::HIGH, 0.7f },
            { 8, Role::LOW, 1.0f }, { 11, Role::HIGH, 0.75f }, { 12, Role::MID, 0.8f },
            { 14, Role::HIGH, 0.7f } },
          0.05, { 2, 5, 10, 13 } } };
    return s;
}

static StyleInfo makeAfro()
{
    StyleInfo s;
    s.interlocking = true;
    s.ornamentDensity = 0.5f;
    s.skeletons = {
        // Son-clave-derived key pattern with interlocking low tumbao.
        { "clave_32", {
            { 0, Role::HIGH, 0.9f }, { 3, Role::HIGH, 0.8f }, { 6, Role::HIGH, 0.85f },
            { 10, Role::HIGH, 0.8f }, { 12, Role::HIGH, 0.85f },
            { 0, Role::LOW, 1.0f }, { 7, Role::LOW, 0.8f }, { 8, Role::LOW, 0.9f } },
          0.15, { 2, 5, 9, 14 } },
        // Euclidean E(5,16) on MID against sparse LOW anchors.
        { "euclid_5_16", {
            { 0, Role::LOW, 1.0f }, { 8, Role::LOW, 0.9f },
            { 0, Role::MID, 0.7f }, { 3, Role::MID, 0.65f }, { 6, Role::MID, 0.7f },
            { 9, Role::MID, 0.65f }, { 12, Role::MID, 0.7f } },
          0.2, { 2, 5, 11, 14, 15 } },
        { "bell_interlock", {
            { 0, Role::LOW, 1.0f }, { 6, Role::LOW, 0.85f }, { 10, Role::LOW, 0.8f },
            { 2, Role::HIGH, 0.75f }, { 5, Role::HIGH, 0.7f }, { 8, Role::HIGH, 0.75f },
            { 12, Role::HIGH, 0.7f }, { 14, Role::HIGH, 0.75f } },
          0.18, { 3, 7, 11, 15 } },
        // Amapiano-leaning: log-drum LOW line lands late, lots of air.
        { "ama_log_line", {
            { 0, Role::LOW, 0.9f }, { 5, Role::LOW, 0.85f }, { 8, Role::LOW, 0.75f },
            { 11, Role::LOW, 0.9f }, { 14, Role::LOW, 0.7f },
            { 4, Role::HIGH, 0.55f }, { 12, Role::HIGH, 0.55f } },
          0.22, { 2, 6, 9, 13 } },
        // Afrobeats 3-3-2 low line with shaker answers.
        { "afrobeats_332", {
            { 0, Role::LOW, 1.0f }, { 3, Role::LOW, 0.8f }, { 6, Role::LOW, 0.85f },
            { 8, Role::LOW, 0.75f }, { 11, Role::LOW, 0.8f }, { 14, Role::LOW, 0.7f },
            { 2, Role::HIGH, 0.6f }, { 10, Role::HIGH, 0.6f } },
          0.25, { 5, 7, 13, 15 } } };
    return s;
}

static StyleInfo makeHybrid()
{
    StyleInfo s;
    s.fourFloorAnchor = true;
    s.ornamentDensity = 0.55f;
    s.skeletons = {
        // EDM anchor + maqsum TAK line: one intentional groove, not a mashup.
        { "hybrid_maqsum_floor", {
            { 0, Role::LOW, 1.0f }, { 4, Role::LOW, 0.9f }, { 8, Role::LOW, 1.0f }, { 12, Role::LOW, 0.9f },
            { 2, Role::HIGH, 0.75f }, { 6, Role::HIGH, 0.7f }, { 12, Role::HIGH, 0.8f },
            { 10, Role::MID, 0.6f } },
          0.1, { 3, 7, 11, 14, 15 } },
        { "hybrid_malfuf_drive", {
            { 0, Role::LOW, 1.0f }, { 4, Role::LOW, 0.9f }, { 8, Role::LOW, 1.0f }, { 12, Role::LOW, 0.9f },
            { 3, Role::HIGH, 0.7f }, { 6, Role::HIGH, 0.7f }, { 11, Role::HIGH, 0.7f }, { 14, Role::HIGH, 0.75f } },
          0.08, { 2, 5, 10, 13 } },
        { "hybrid_afro_floor", {
            { 0, Role::LOW, 1.0f }, { 4, Role::LOW, 0.85f }, { 8, Role::LOW, 1.0f }, { 12, Role::LOW, 0.85f },
            { 3, Role::MID, 0.65f }, { 7, Role::MID, 0.6f }, { 10, Role::MID, 0.65f }, { 14, Role::MID, 0.6f } },
          0.16, { 2, 5, 9, 13, 15 } },
        // Sa'idi weight on an EDM frame: the double DUM in the middle.
        { "hybrid_saidi_floor", {
            { 0, Role::LOW, 1.0f }, { 4, Role::LOW, 0.9f }, { 8, Role::LOW, 1.0f }, { 12, Role::LOW, 0.9f },
            { 6, Role::LOW, 0.8f }, { 2, Role::HIGH, 0.7f }, { 12, Role::HIGH, 0.75f }, { 14, Role::MID, 0.6f } },
          0.1, { 3, 7, 11, 15 } },
        // Clave cutting through a floor: HIGH keeps 3-2, LOW keeps four.
        { "hybrid_clave_floor", {
            { 0, Role::LOW, 1.0f }, { 4, Role::LOW, 0.85f }, { 8, Role::LOW, 1.0f }, { 12, Role::LOW, 0.85f },
            { 0, Role::HIGH, 0.8f }, { 3, Role::HIGH, 0.7f }, { 6, Role::HIGH, 0.75f },
            { 10, Role::HIGH, 0.7f }, { 12, Role::HIGH, 0.75f } },
          0.14, { 2, 5, 9, 14, 15 } } };
    return s;
}

const StyleInfo& RhythmStyle::get (Family family)
{
    static const StyleInfo edm = makeEdm();
    static const StyleInfo arabic = makeArabic();
    static const StyleInfo med = makeMediterranean();
    static const StyleInfo afro = makeAfro();
    static const StyleInfo hybrid = makeHybrid();

    switch (family)
    {
        case Family::EDM:           return edm;
        case Family::ARABIC:        return arabic;
        case Family::MEDITERRANEAN: return med;
        case Family::AFRO:          return afro;
        case Family::HYBRID:        return hybrid;
    }
    return edm;
}

} // namespace orcha
