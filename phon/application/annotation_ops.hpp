/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2026 Julien Eychenne                                                                                  *
 *                                                                                                                     *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public   *
 * License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any      *
 * later version.                                                                                                      *
 *                                                                                                                     *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied  *
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more        *
 * details.                                                                                                            *
 *                                                                                                                     *
 * You should have received a copy of the GNU General Public License along with this program. If not, see              *
 * <http://www.gnu.org/licenses/>.                                                                                     *
 *                                                                                                                     *
 * Created: 12/05/2026                                                                                                 *
 *                                                                                                                     *
 * Purpose: structural transformations on annotations and sounds. Each function produces a *new* document under a      *
 * given output path, writes it to disk, and returns a freshly-allocated Handle<>. None of these functions touch the   *
 * Project: the caller (a GUI dialog or a scripting binding) decides whether and how to add the result to the project. *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#ifndef PHONOMETRICA_ANNOTATION_OPS_HPP
#define PHONOMETRICA_ANNOTATION_OPS_HPP

#include <span>
#include <phon/application/annotation.hpp>
#include <phon/application/sound.hpp>

namespace phonometrica {

// Default tolerance (in seconds) for considering two annotation durations
// "the same" in merge_annotations. One millisecond is one frame at 1 kHz — well
// below the time resolution of any practical phonetic workflow.
constexpr double DEFAULT_DURATION_TOLERANCE = 1e-3;


//----------------------------------------------------------------------------------------------------------------------
//  Annotation operations
//----------------------------------------------------------------------------------------------------------------------


// Duplicate `src` under `out_path`. The output format follows the source.
// Properties, description, and sound binding are preserved.
Handle<Annotation>
duplicate_annotation(Annotation &src, const String &out_path);


// Extract the layers at the given 1-based indices, in input order, into a new
// annotation. Indices must be distinct, within range, and the span must be
// non-empty. The sound binding is preserved (duration unchanged). Properties
// and description are inherited from the source.
//
// `output_format` chooses the on-disk serialization format. Pass
// Annotation::Type::Native or Annotation::Type::TextGrid; Undefined defers
// to the source's format.
Handle<Annotation>
extract_layers(Annotation &src,
               std::span<const intptr_t> layer_indices,
               const String &out_path,
               Annotation::Type output_format = Annotation::Type::Undefined);


// Merge `base`'s layers with the layers of every annotation in `others`. All
// inputs must have the same duration within `tolerance`. Duration is defined
// as the bound sound's duration when the annotation has a sound binding, and
// as the max event end time across all layers otherwise.
//
// Layers from `others` are appended to a deep copy of `base`'s layers in the
// order they appear in the span. Label collisions are disambiguated by
// appending " (2)", " (3)", … to the duplicate.
//
// Sound binding is inherited from `base`. Properties: every property of every
// input is copied into the result; on category collision, the later input
// wins (i.e. the value from `others[k]` overrides `others[j]` for j < k, and
// any of those override `base`). Description is taken from `base`.
Handle<Annotation>
merge_annotations(Annotation &base,
                  std::span<const Handle<Annotation>> others,
                  const String &out_path,
                  Annotation::Type output_format = Annotation::Type::Undefined,
                  double duration_tolerance = DEFAULT_DURATION_TOLERANCE);


// Extract the time slice [t_start, t_end] from `src` into a new annotation.
// Times must satisfy 0 <= t_start < t_end. The returned annotation's events
// are shifted by -t_start so that the slice starts at zero.
//
// Events fully outside the slice are dropped. Events fully inside are kept
// verbatim (only shifted). Events partially overlapping the boundaries are
// clipped to fit when `clip_partial` is true and dropped otherwise. Instants
// are never "partial" — they are kept iff strictly inside [t_start, t_end];
// the `clip_partial` flag does not affect them.
//
// The sound binding is NOT inherited: the bound sound no longer matches the
// new duration. Callers that also want to extract the matching sound slice
// should call `extract_sound_slice` separately and bind the result.
Handle<Annotation>
extract_annotation_slice(Annotation &src,
                         double t_start, double t_end,
                         bool clip_partial,
                         const String &out_path,
                         Annotation::Type output_format = Annotation::Type::Undefined);


// Concatenate the annotations in `sources` end-to-end. All sources must have
// the same number of layers, and the layer at each index must have the same
// kind (interval vs. instant) across all sources. Layer labels are taken from
// the first source; subsequent label mismatches at the same index are not an
// error — the first wins silently. The empty `sources` span is rejected.
//
// `durations` gives each source's effective duration. Pass an empty span to
// infer durations from the bound sound of each source: every source must then
// be bound, otherwise the call throws. When provided explicitly, `durations`
// must have the same size as `sources`.
//
// Events from source `i` are shifted by the cumulative duration offset
// D_0 + D_1 + … + D_{i-1}. Adjacent intervals at boundaries (last interval of
// source `i`, first interval of source `i+1`) are NOT merged into one — they
// remain distinct events. Their text labels may differ, and merging would
// lose information.
//
// Sound binding is NOT inherited (the result spans multiple sounds). Callers
// that want a bound result should call `concatenate_sounds` and bind manually.
// Properties: every property of every input is copied into the result; on
// category collision, the later source wins. Description from `sources[0]`.
Handle<Annotation>
concatenate_annotations(std::span<const Handle<Annotation>> sources,
                        std::span<const double> durations,
                        const String &out_path,
                        Annotation::Type output_format = Annotation::Type::Undefined);


//----------------------------------------------------------------------------------------------------------------------
//  Sound operations
//----------------------------------------------------------------------------------------------------------------------


// Extract samples in [t_start, t_end] from `src` into a new sound file at
// `out_path`. Times must satisfy 0 <= t_start < t_end <= duration. Sample
// rate and channel count are preserved; the format follows `format`. The
// data is streamed frame-by-frame through libsndfile — the source's full
// sample buffer is not loaded into memory.
Handle<Sound>
extract_sound_slice(Sound &src,
                    double t_start, double t_end,
                    const String &out_path,
                    Sound::Format format);


// Concatenate `sources` end-to-end into a new sound file. Strict mode:
// all sources must share the same sample rate and channel count. Output keeps
// the common rate and channel count, with the chosen format. Data is streamed
// through libsndfile.
//
// Resampling-on-concat is a planned follow-up; for now, a sample-rate or
// channel-count mismatch is a hard error pointing to the first offending file.
Handle<Sound>
concatenate_sounds(std::span<const Handle<Sound>> sources,
                   const String &out_path,
                   Sound::Format format);


// Convert `src` to a new sound file at `out_path`, in the chosen `format`,
// optionally resampling to `target_sample_rate`. Channel count is preserved.
// Each channel is resampled independently (r8brain CDSPResampler24) to keep
// the resampler state per-channel.
//
// `target_sample_rate <= 0`, or equal to the source's sample rate, means
// "no resampling" — frames are streamed directly. Otherwise the expected
// output frame count is precomputed and the loop drains the resampler tail
// with zero-padding once the input is exhausted (matching the convention
// used by the standalone resample() helper).
//
// Data is streamed through libsndfile; the source's full sample buffer is
// not loaded into memory.
Handle<Sound>
convert_sound(Sound &src,
              const String &out_path,
              Sound::Format format,
              int target_sample_rate = 0);


//----------------------------------------------------------------------------------------------------------------------
//  Helpers
//----------------------------------------------------------------------------------------------------------------------


// Pick a non-colliding sibling path. Tries `desired_path` first, then
// `name (2).ext`, `name (3).ext`, … checking both the filesystem and the
// current Project's set of registered paths. Returns the first available path.
String unique_path(const String &desired_path);


// Compute an annotation's effective duration. Uses the bound sound's
// duration if the annotation has one; otherwise returns the maximum event
// end time across all layers, or zero for an empty annotation.
double effective_duration(const Annotation &annot);


// Infer the sound output format from a path's extension. Recognized
// extensions: .wav, .aiff/.aif, .flac, .ogg. Throws on any other extension.
Sound::Format sound_format_from_path(const String &path);


} // namespace phonometrica

#endif // PHONOMETRICA_ANNOTATION_OPS_HPP
