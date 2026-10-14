#pragma once
#include "features/information/TargetInfo.h"
#include "features/schematic/Verification.h"
#include <cstdlib>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

// The target card's lower schematic section (L-184, 2026-10-15).
// Pure; the caller supplies translation.
namespace lamium::information {
using Translate = std::function<std::string(std::string_view)>;
// A raw state value as the card shows it: by name when L-112 knows it.
struct StateName { std::string label, value; bool labelIsKey = false; };
inline StateName stateName(std::string const& key, std::string const& raw, std::string_view identifier,
                           Translate const& translate) {
    if (raw == "-") return {key, raw};
    std::optional<TargetInfo::DetailRow> row;
    char* end = nullptr;
    long long number = std::strtoll(raw.c_str(), &end, 10);
    if (!raw.empty() && end && *end == '\0') row = interpretBlockState(key, StateKind::Integer, number, {}, identifier);
    else row = interpretBlockState(key, StateKind::Text, 0, raw, identifier);
    if (!row || row->progress) return {key, raw};
    return {row->label, row->valueIsKey ? translate(row->value) : row->value, true};
}
inline std::vector<TargetInfo::DetailRow> schematicStateRows(
    std::vector<schematic::StateDifference> const& differences, std::string_view identifier,
    Translate const& translate) {
    std::vector<TargetInfo::DetailRow> rows;
    for (auto const& d : differences) {
        auto now = stateName(d.key, d.actual, identifier, translate);
        auto want = stateName(d.key, d.expected, identifier, translate);
        auto const& named = now.labelIsKey ? now : want;
        TargetInfo::DetailRow row{named.label, want.value};
        row.before = now.value;
        row.kind = DetailKind::Schematic;
        row.labelIsKey = named.labelIsKey;
        row.tone = Tone::State;
        rows.push_back(std::move(row));
    }
    return rows;
}
inline TargetInfo makeGhostTarget(TargetInfo target) {
    TargetInfo::Schematic section;
    section.name = target.name;
    section.identifier = target.identifier;
    section.icon = target.icon;
    section.status = "schematic.kind.missing";
    section.tone = Tone::Missing;
    section.details = target.details;
    section.states = target.states;
    section.blockPosition = target.blockPosition;
    target.schematic = std::move(section);
    target.ghostOnly = true;
    return target;
}
inline std::optional<TargetInfo::Schematic> schematicTarget(schematic::Mismatch const& mismatch, Translate const& translate) {
    using schematic::CellState;
    if (mismatch.entity) return std::nullopt;
    TargetInfo::Schematic result;
    result.name = mismatch.expectedName;
    result.item = mismatch.expected;
    switch (mismatch.state) {
    case CellState::Wrong: result.status = "target.schematicWrong"; result.tone = Tone::Wrong; break;
    case CellState::Extra:
        result.status = "target.schematicExtra";
        result.tone = Tone::Wrong;
        result.name = translate("schematic.air");
        result.item.clear();
        break;
    case CellState::Missing: result.status = "schematic.kind.missing"; result.tone = Tone::Missing; break;
    case CellState::State:
        result.status = "target.schematicState";
        result.tone = Tone::State;
        result.details = schematicStateRows(mismatch.states, mismatch.identifier, translate);
        break;
    default: return std::nullopt;
    }
    return result;
}
}
