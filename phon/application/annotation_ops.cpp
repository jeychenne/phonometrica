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
 * Purpose: see header.                                                                                                *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <algorithm>
#include <vector>
#include <sndfile.hh>
#include <phon/application/annotation_ops.hpp>
#include <phon/application/project.hpp>
#include <phon/utils/file_system.hpp>
#include <phon/error.hpp>

namespace phonometrica {

namespace {

// Streaming I/O buffer (frames). Large enough to amortize the per-call cost of
// libsndfile read/write but small enough to keep stack/heap pressure low.
constexpr int IO_BUFFER_FRAMES = 4096;


// Pick a libsndfile subtype for the chosen output format. PCM_16 is a sensible
// default for the lossless containers (WAV, AIFF, FLAC). OGG must use VORBIS.
// Callers that need a different bit depth can extend Sound::Format in the
// future; that change does not belong in this file.
int default_subtype_for(Sound::Format format)
{
	switch (format)
	{
		case Sound::Format::WAV:
		case Sound::Format::AIFF:
		case Sound::Format::FLAC:
			return SF_FORMAT_PCM_16;
		case Sound::Format::OGG:
			return SF_FORMAT_VORBIS;
	}
	return SF_FORMAT_PCM_16;
}


// Open a SndfileHandle for reading from a path, with the appropriate wide-path
// dance on Windows. Returns a closed/invalid handle on failure; the caller is
// expected to check via the bool conversion.
SndfileHandle open_reader(const String &path)
{
#if PHON_WINDOWS
	auto wpath = path.to_wide();
	return SndfileHandle(wpath.data());
#else
	return SndfileHandle(path.data());
#endif
}


// Open a SndfileHandle for writing.
SndfileHandle open_writer(const String &path, int format, int channels, int sample_rate)
{
#if PHON_WINDOWS
	auto wpath = path.to_wide();
	return SndfileHandle(wpath.data(), SFM_WRITE, format, channels, sample_rate);
#else
	return SndfileHandle(path.data(), SFM_WRITE, format, channels, sample_rate);
#endif
}


// Resolve the requested output format against a fallback (the source's format,
// or the base/first input's). Undefined means "follow the fallback".
Annotation::Type resolve_format(Annotation::Type requested, Annotation::Type fallback)
{
	if (requested == Annotation::Type::Undefined)
		return fallback;
	return requested;
}


// Write `annot` to disk in the given format, then set its path so that its
// in-memory type matches what's on disk.
void write_in_format(Annotation &annot, const String &out_path, Annotation::Type format)
{
	switch (format)
	{
		case Annotation::Type::Native:
			annot.write_as_native(out_path);
			break;
		case Annotation::Type::TextGrid:
			annot.write_as_textgrid(out_path);
			break;
		default:
			throw error("Cannot write annotation: unsupported output format");
	}
	annot.set_path(out_path, false);
}


// Disambiguate a layer label against the labels already present in `existing`.
// Returns the input unchanged if it doesn't collide; otherwise appends
// " (2)", " (3)", … until a free name is found.
String disambiguate_label(const Array<Layer> &existing, const String &label)
{
	auto label_used = [&](const String &candidate) {
		for (auto &L : existing) {
			if (L.label == candidate) return true;
		}
		return false;
	};

	if (!label_used(label))
		return label;

	for (int n = 2; ; ++n)
	{
		auto candidate = String::format("%s (%d)", label.data(), n);
		if (!label_used(candidate))
			return candidate;
	}
}


// Validate that `indices` is a non-empty, in-range, duplicate-free set of
// 1-based layer indices into an annotation with `layer_count` layers.
void validate_layer_indices(std::span<const intptr_t> indices, intptr_t layer_count)
{
	if (indices.empty())
		throw error("[Argument error] No layer indices given for extraction");

	for (intptr_t idx : indices)
	{
		if (idx < 1 || idx > layer_count) {
			throw error("[Index error] Layer index % out of range (annotation has % layer(s))",
			            idx, layer_count);
		}
	}

	// Duplicate check (small N, linear scan is fine).
	for (size_t i = 0; i < indices.size(); ++i) {
		for (size_t j = i + 1; j < indices.size(); ++j) {
			if (indices[i] == indices[j])
				throw error("[Argument error] Duplicate layer index % in extraction list", indices[i]);
		}
	}
}


// Clip an interval [s, e] to [t1, t2]. Returns true if any overlap exists.
bool clip_interval(double &s, double &e, double t1, double t2)
{
	if (e <= t1 || s >= t2) return false;
	s = std::max(s, t1);
	e = std::min(e, t2);
	return s < e;
}


// Slice events from `src_events` into `dst_events`, applying the
// [t_start, t_end] policy described in the header of extract_annotation_slice.
// Times in the destination are shifted by -t_start so the slice starts at zero.
void slice_events(const Array<Event> &src_events, Array<Event> &dst_events,
                  double t_start, double t_end, bool has_instants, bool clip_partial)
{
	for (auto &ev : src_events)
	{
		if (has_instants)
		{
			// Instants: kept iff inside [t_start, t_end] (closed on both ends).
			if (ev.start >= t_start && ev.start <= t_end) {
				double t = ev.start - t_start;
				dst_events.append(Event { t, t, ev.text });
			}
			continue;
		}

		// Intervals: fully outside?
		if (ev.end <= t_start || ev.start >= t_end)
			continue;

		bool fully_inside = (ev.start >= t_start && ev.end <= t_end);
		if (fully_inside) {
			dst_events.append(Event { ev.start - t_start, ev.end - t_start, ev.text });
			continue;
		}

		// Partial overlap.
		if (!clip_partial) continue;

		double s = ev.start, e = ev.end;
		if (!clip_interval(s, e, t_start, t_end))
			continue;
		dst_events.append(Event { s - t_start, e - t_start, ev.text });
	}
}


// Copy properties from `src` into `dst`. Existing properties in `dst` whose
// category matches one in `src` are replaced (Document::add_property already
// does "remove-by-category-then-insert").
void overlay_properties(Annotation &dst, const Annotation &src)
{
	for (auto &p : src.properties()) {
		dst.add_property(p, false);
	}
}


} // anonymous namespace


//======================================================================================================================
//  Helpers (exported)
//======================================================================================================================


double effective_duration(const Annotation &annot)
{
	if (annot.has_sound()) {
		return annot.sound()->duration();
	}
	double max_end = 0.0;
	for (auto &layer : annot.layers())
	{
		if (!layer.events.empty()) {
			// Events are sorted by start time, and intervals in a Phonometrica
			// layer never overlap, so the last event has the maximum end time.
			max_end = std::max(max_end, layer.events.last().end);
		}
	}
	return max_end;
}


String unique_path(const String &desired_path)
{
	auto project = Project::get();

	auto path_taken = [&](const String &p) {
		if (filesystem::exists(p)) return true;
		if (project) {
			auto &files = project->files();
			if (files.find(p) != files.end()) return true;
		}
		return false;
	};

	if (!path_taken(desired_path))
		return desired_path;

	auto pair = filesystem::split_ext(desired_path);
	auto &stem = pair.first;
	auto &ext  = pair.second;  // includes leading dot (or empty)

	for (int n = 2; ; ++n)
	{
		auto candidate = String::format("%s (%d)%s", stem.data(), n, ext.data());
		if (!path_taken(candidate))
			return candidate;
	}
}


Sound::Format sound_format_from_path(const String &path)
{
	auto ext = filesystem::ext(path, true);  // lowercased, with leading dot
	if (ext == ".wav")  return Sound::Format::WAV;
	if (ext == ".aiff" || ext == ".aif") return Sound::Format::AIFF;
	if (ext == ".flac") return Sound::Format::FLAC;
	if (ext == ".ogg")  return Sound::Format::OGG;
	throw error("[Argument error] Cannot infer sound format from path extension: %", path);
}


//======================================================================================================================
//  Annotation operations
//======================================================================================================================


Handle<Annotation> duplicate_annotation(Annotation &src, const String &out_path)
{
	src.open();

	auto result = make_handle<Annotation>();

	// Layers.
	for (auto &layer : src.layers()) {
		result->append_layer(layer.duplicate(layer.label));
	}

	// Properties & description.
	overlay_properties(*result, src);
	if (!src.description().empty()) {
		result->set_description(src.description(), false);
	}

	// Sound binding (duration unchanged, binding stays consistent).
	if (src.has_sound()) {
		result->set_sound(src.sound(), false);
	}

	// Choose format: follow source.
	Annotation::Type out_format = src.is_textgrid() ? Annotation::Type::TextGrid
	                                                : Annotation::Type::Native;
	write_in_format(*result, out_path, out_format);

	return result;
}


Handle<Annotation>
extract_layers(Annotation &src,
               std::span<const intptr_t> layer_indices,
               const String &out_path,
               Annotation::Type output_format)
{
	src.open();
	validate_layer_indices(layer_indices, src.layer_count());

	auto result = make_handle<Annotation>();

	for (intptr_t idx : layer_indices) {
		const auto &layer = src.layers()[idx];
		result->append_layer(layer.duplicate(layer.label));
	}

	overlay_properties(*result, src);
	if (!src.description().empty()) {
		result->set_description(src.description(), false);
	}
	if (src.has_sound()) {
		result->set_sound(src.sound(), false);
	}

	auto fallback = src.is_textgrid() ? Annotation::Type::TextGrid : Annotation::Type::Native;
	write_in_format(*result, out_path, resolve_format(output_format, fallback));

	return result;
}


Handle<Annotation>
merge_annotations(Annotation &base,
                  std::span<const Handle<Annotation>> others,
                  const String &out_path,
                  Annotation::Type output_format,
                  double duration_tolerance)
{
	base.open();
	for (auto &h : others) h->open();

	// Duration check.
	double base_dur = effective_duration(base);
	for (size_t i = 0; i < others.size(); ++i)
	{
		double d = effective_duration(*others[i]);
		if (std::abs(d - base_dur) > duration_tolerance) {
			throw error("[Merge error] Duration mismatch: base annotation has duration % s, "
			            "annotation #% has duration % s (tolerance: % s)",
			            base_dur, intptr_t(i + 1), d, duration_tolerance);
		}
	}

	auto result = make_handle<Annotation>();

	// Layers: base first, then each "other".
	for (auto &layer : base.layers()) {
		result->append_layer(layer.duplicate(layer.label));
	}
	for (auto &h : others) {
		for (auto &layer : h->layers()) {
			auto label = disambiguate_label(result->layers(), layer.label);
			result->append_layer(layer.duplicate(label));
		}
	}

	// Properties: base then overlay each subsequent (last writer wins by category).
	overlay_properties(*result, base);
	for (auto &h : others) {
		overlay_properties(*result, *h);
	}

	// Description from base.
	if (!base.description().empty()) {
		result->set_description(base.description(), false);
	}

	// Sound binding from base.
	if (base.has_sound()) {
		result->set_sound(base.sound(), false);
	}

	auto fallback = base.is_textgrid() ? Annotation::Type::TextGrid : Annotation::Type::Native;
	write_in_format(*result, out_path, resolve_format(output_format, fallback));

	return result;
}


Handle<Annotation>
extract_annotation_slice(Annotation &src,
                         double t_start, double t_end,
                         bool clip_partial,
                         const String &out_path,
                         Annotation::Type output_format)
{
	if (!(t_start >= 0 && t_start < t_end)) {
		throw error("[Argument error] Invalid slice [% s, % s]: require 0 <= t_start < t_end",
		            t_start, t_end);
	}
	src.open();

	auto result = make_handle<Annotation>();

	for (auto &src_layer : src.layers())
	{
		Layer dst(src_layer.label, src_layer.has_instants);
		dst.events.reserve(src_layer.events.size());
		slice_events(src_layer.events, dst.events, t_start, t_end,
		             src_layer.has_instants, clip_partial);
		result->append_layer(std::move(dst));
	}

	overlay_properties(*result, src);
	if (!src.description().empty()) {
		result->set_description(src.description(), false);
	}
	// Sound binding deliberately NOT inherited: the bound sound no longer
	// matches the new duration. Callers wanting a bound slice should extract
	// the matching sound slice separately and call set_sound() on the result.

	auto fallback = src.is_textgrid() ? Annotation::Type::TextGrid : Annotation::Type::Native;
	write_in_format(*result, out_path, resolve_format(output_format, fallback));

	return result;
}


Handle<Annotation>
concatenate_annotations(std::span<const Handle<Annotation>> sources,
                        std::span<const double> durations,
                        const String &out_path,
                        Annotation::Type output_format)
{
	if (sources.empty())
		throw error("[Argument error] concatenate_annotations: empty source list");

	for (auto &h : sources) h->open();

	// Resolve durations.
	std::vector<double> dur(sources.size());
	if (durations.empty())
	{
		for (size_t i = 0; i < sources.size(); ++i)
		{
			if (!sources[i]->has_sound()) {
				throw error("[Concatenation error] Source #% has no bound sound: pass an explicit "
				            "duration for each unbound annotation", intptr_t(i + 1));
			}
			dur[i] = sources[i]->sound()->duration();
		}
	}
	else
	{
		if (durations.size() != sources.size()) {
			throw error("[Argument error] concatenate_annotations: % durations given for % sources",
			            intptr_t(durations.size()), intptr_t(sources.size()));
		}
		for (size_t i = 0; i < sources.size(); ++i)
		{
			if (!(durations[i] > 0)) {
				throw error("[Argument error] Non-positive duration % s for source #%",
				            durations[i], intptr_t(i + 1));
			}
			dur[i] = durations[i];
		}
	}

	// Shape check: same layer count, matching kind at each index.
	const auto &first = *sources[0];
	intptr_t nlayers = first.layer_count();
	for (size_t i = 1; i < sources.size(); ++i)
	{
		auto &s = *sources[i];
		if (s.layer_count() != nlayers) {
			throw error("[Concatenation error] Layer count mismatch: source #1 has % layers, "
			            "source #% has % layers", nlayers, intptr_t(i + 1), s.layer_count());
		}
		for (intptr_t k = 1; k <= nlayers; ++k) {
			if (s.layer_has_instants(k) != first.layer_has_instants(k)) {
				throw error("[Concatenation error] Layer % kind mismatch between source #1 "
				            "and source #%: one is an instant layer, the other is an interval layer",
				            k, intptr_t(i + 1));
			}
		}
	}

	auto result = make_handle<Annotation>();

	// Build each output layer by concatenating events with cumulative offsets.
	for (intptr_t k = 1; k <= nlayers; ++k)
	{
		const auto &model = first.layers()[k];
		Layer out_layer(model.label, model.has_instants);

		// Reserve a reasonable lower bound on capacity.
		intptr_t total_events = 0;
		for (auto &h : sources) total_events += h->layers()[k].events.size();
		out_layer.events.reserve(total_events);

		double offset = 0.0;
		for (size_t i = 0; i < sources.size(); ++i)
		{
			const auto &in_layer = sources[i]->layers()[k];
			for (auto &ev : in_layer.events) {
				out_layer.events.append(Event { ev.start + offset, ev.end + offset, ev.text });
			}
			offset += dur[i];
		}

		result->append_layer(std::move(out_layer));
	}

	// Properties: first then overlay each subsequent.
	overlay_properties(*result, first);
	for (size_t i = 1; i < sources.size(); ++i) {
		overlay_properties(*result, *sources[i]);
	}

	if (!first.description().empty()) {
		result->set_description(first.description(), false);
	}
	// No sound binding (the concatenation spans multiple sounds).

	auto fallback = first.is_textgrid() ? Annotation::Type::TextGrid : Annotation::Type::Native;
	write_in_format(*result, out_path, resolve_format(output_format, fallback));

	return result;
}


//======================================================================================================================
//  Sound operations
//======================================================================================================================


Handle<Sound>
extract_sound_slice(Sound &src,
                    double t_start, double t_end,
                    const String &out_path,
                    Sound::Format format)
{
	if (!(t_start >= 0 && t_start < t_end && t_end <= src.duration() + 1e-9)) {
		throw error("[Argument error] Invalid sound slice [% s, % s]: require "
		            "0 <= t_start < t_end <= duration (% s)",
		            t_start, t_end, src.duration());
	}

	auto reader = open_reader(src.path());
	if (!reader) {
		throw error("[I/O error] Cannot open sound file for reading: %", src.path());
	}

	int channels    = reader.channels();
	int sample_rate = reader.samplerate();
	int out_format  = static_cast<int>(format) | default_subtype_for(format);

	auto writer = open_writer(out_path, out_format, channels, sample_rate);
	if (!writer) {
		throw error("[I/O error] Cannot open sound file for writing: %", out_path);
	}

	// Frame indices. Use the sound's own time_to_frame for consistency with
	// any other code that maps times to frames against the same file. Clamp
	// to the file's frame count.
	intptr_t start_frame = src.time_to_frame(t_start);
	intptr_t end_frame   = src.time_to_frame(t_end);
	intptr_t total = (intptr_t) reader.frames();
	if (start_frame < 0)     start_frame = 0;
	if (end_frame   > total) end_frame   = total;
	if (start_frame >= end_frame) {
		throw error("[Argument error] Empty slice after frame rounding");
	}

	if (reader.seek(start_frame, SEEK_SET) < 0) {
		throw error("[I/O error] Failed to seek to frame % in %", start_frame, src.path());
	}

	std::vector<float> buf(IO_BUFFER_FRAMES * channels);
	intptr_t frames_left = end_frame - start_frame;

	while (frames_left > 0)
	{
		auto to_read = std::min<intptr_t>(IO_BUFFER_FRAMES, frames_left);
		auto got = reader.readf(buf.data(), to_read);
		if (got <= 0) break;
		writer.writef(buf.data(), got);
		frames_left -= got;
	}

	// Flush the writer by destroying its handle (RAII closes the file).
	writer = SndfileHandle();

	return make_handle<Sound>(nullptr, out_path);
}


Handle<Sound>
concatenate_sounds(std::span<const Handle<Sound>> sources,
                   const String &out_path,
                   Sound::Format format)
{
	if (sources.empty())
		throw error("[Argument error] concatenate_sounds: empty source list");

	int channels    = sources[0]->nchannel();
	int sample_rate = sources[0]->sample_rate();

	for (size_t i = 1; i < sources.size(); ++i)
	{
		if (sources[i]->nchannel() != channels) {
			throw error("[Concatenation error] Channel count mismatch: source #1 has %, "
			            "source #% has %", channels, intptr_t(i + 1), sources[i]->nchannel());
		}
		if (sources[i]->sample_rate() != sample_rate) {
			throw error("[Concatenation error] Sample-rate mismatch: source #1 is % Hz, "
			            "source #% is % Hz. Resampling on concatenation is not yet supported; "
			            "resample the inputs to a common rate first.",
			            sample_rate, intptr_t(i + 1), sources[i]->sample_rate());
		}
	}

	int out_format = static_cast<int>(format) | default_subtype_for(format);
	auto writer = open_writer(out_path, out_format, channels, sample_rate);
	if (!writer) {
		throw error("[I/O error] Cannot open sound file for writing: %", out_path);
	}

	std::vector<float> buf(IO_BUFFER_FRAMES * channels);

	for (size_t i = 0; i < sources.size(); ++i)
	{
		auto reader = open_reader(sources[i]->path());
		if (!reader) {
			throw error("[I/O error] Cannot open sound file for reading: %", sources[i]->path());
		}
		reader.seek(0, SEEK_SET);

		intptr_t frames_left = (intptr_t) reader.frames();
		while (frames_left > 0)
		{
			auto to_read = std::min<intptr_t>(IO_BUFFER_FRAMES, frames_left);
			auto got = reader.readf(buf.data(), to_read);
			if (got <= 0) break;
			writer.writef(buf.data(), got);
			frames_left -= got;
		}
	}

	writer = SndfileHandle();  // flush via RAII

	return make_handle<Sound>(nullptr, out_path);
}


} // namespace phonometrica
