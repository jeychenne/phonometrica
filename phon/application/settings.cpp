/***********************************************************************************************************************
 *                                                                                                                     *
 * Copyright (C) 2019-2026 Julien Eychenne                                                                             *
 *                                                                                                                     *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public   *
 * License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any      *
 * later version.                                                                                                      *
 *                                                                                                                     *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied  *
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more       *
 * details.                                                                                                            *
 *                                                                                                                     *
 * You should have received a copy of the GNU General Public License along with this program. If not, see              *
 * <http://www.gnu.org/licenses/>.                                                                                     *
 *                                                                                                                     *
 * Created: 28/02/2019                                                                                                 *
 *                                                                                                                     *
 * purpose: see header. Since the A1 base-type swap the settings store lives in the NEW engine: `phon` is a Table      *
 * held as an isolate global and `phon.settings` is a nested Table. Both are CoW values (engine roadmap E2), so        *
 * every C++-side mutation is a read-modify-write: fetch the tables from the global, set the key (which may detach),   *
 * and write the result back through add_global. Never cache a Table across calls.                                     *
 *                                                                                                                     *
 ***********************************************************************************************************************/

#include <cctype>
#include <phon/application/constants.hpp>
#include <phon/application/settings.hpp>
#include <phon/definitions.hpp>
#include <phon/error.hpp>
#include <phon/utils/file_system.hpp>
#include <phon/file.hpp>

namespace phonometrica {

Runtime *Settings::runtime = nullptr;
String Settings::std_resource_path;
std::function<String(const String &)> Settings::load_script;

static String settings_key("settings");
static String last_dir_key("last_directory");

// --- CoW plumbing -----------------------------------------------------------------

static Variant str_key(const String &s)
{
	return Variant::make(s);
}

static Variant str_key(const char *s)
{
	return Variant::make(String(s));
}

// Fetch phon.settings. Throws if the phon namespace or the settings table is missing.
static Table fetch_settings(Runtime *rt)
{
	auto phon = rt->get_global("phon");
	if (phon.is_null()) {
		throw error("[Internal error] The 'phon' namespace is not loaded");
	}
	return phon.to<Table>().get(str_key(settings_key)).to<Table>();
}

// Store a (possibly detached) settings table back into the phon global.
static void store_settings(Runtime *rt, const Table &settings)
{
	auto phon = rt->get_global("phon").to<Table>();
	phon.set(str_key(settings_key), Variant::make(settings));
	rt->add_global("phon", Variant::make(phon));
}

void Settings::initialize(Runtime *rt)
{
	runtime = rt;
	using namespace filesystem;

#if PHON_WINDOWS
	std_resource_path = directory_name(rt->program_path());
#elif PHON_MACOS
	std_resource_path = directory_name(directory_name(directory_name(rt->program_path())));
#else
	std_resource_path = "/usr/local/share/phonometrica";
#endif

	// Create global functions related to settings
	rt->add_function("get_settings_directory", []() -> String {
		return Settings::settings_directory();
	});
	rt->add_function("get_metadata_directory", []() -> String {
		return Settings::metadata_directory();
	});
	rt->add_function("get_plugin_directory", []() -> String {
		return Settings::plugin_directory();
	});
	rt->add_function("get_script_directory", []() -> String {
		return Settings::user_script_directory();
	});
	rt->add_function("get_config_path", []() -> String {
		return Settings::config_path();
	});
}

String Settings::settings_directory()
{
#if PHON_LINUX
	auto name = "phonometrica";
#else
	auto name = "Phonometrica";
#endif

	return filesystem::join(filesystem::application_directory(), name);
}

String Settings::plugin_directory()
{
	auto path = settings_directory();
	filesystem::append(path, "Plugins");

	return path;
}

String Settings::metadata_directory()
{
	auto path = settings_directory();
	filesystem::append(path, "Metadata");

	return path;
}

String Settings::user_script_directory()
{
	auto path = settings_directory();
	filesystem::append(path, "Scripts");

	return path;
}

String Settings::config_path()
{
	auto path = settings_directory();
	filesystem::append(path, "settings.phon");

	return path;
}

String Settings::get_string(const String &name)
{
	try
	{
		// Get "phon.settings.name"
		return fetch_settings(runtime).get(str_key(name)).to<String>();
	}
	catch (std::runtime_error &e)
	{
		throw error("Invalid setting \"%\": %", name, e.what());
	}
}

bool Settings::get_boolean(const String &name)
{
	try
	{
		return fetch_settings(runtime).get(str_key(name)).to<bool>();
	}
	catch (std::runtime_error &e)
	{
		throw error("Invalid setting \"%\": %", name, e.what());
	}
}

double Settings::get_number(const String &name)
{
	try
	{
		return fetch_settings(runtime).get(str_key(name)).to<double>();
	}
	catch (std::runtime_error &e)
	{
		throw error("Invalid setting \"%\": %", name, e.what());
	}
}

List Settings::get_list(const String &name)
{
	try
	{
		return fetch_settings(runtime).get(str_key(name)).to<List>();
	}
	catch (std::runtime_error &e)
	{
		throw error("Invalid setting \"%\": %", name, e.what());
	}
}

String Settings::get_std_script(String name)
{
	auto path = Settings::resources_directory();
	filesystem::append(path, "std");
	name = filesystem::nativize(name);
	name.append(PHON_EXT_SCRIPT);
	filesystem::append(path, name);

	return path;
}

String Settings::get_last_directory()
{
	return get_string(last_dir_key);
}

void Settings::set_value(const String &key, Variant value)
{
	auto settings = fetch_settings(runtime);
	settings.set(str_key(key), std::move(value));
	store_settings(runtime, settings);
}

void Settings::set_value(const String &key, Array<Variant> value)
{
	List list;
	for (auto &v : value) {
		list.append(v);
	}
	set_value(key, Variant::make(list));
}


void Settings::set_last_directory(const String &path)
{
	if (!path.empty()) {
		set_value(last_dir_key, Variant::make(filesystem::directory_name(path)));
	}
}

int Settings::get_int(const String &name)
{
	return int(get_number(name));
}

int Settings::get_int(const String &category, const String &name)
{
	// Get "phon.settings.category.name"
	auto mod = fetch_settings(runtime).get(str_key(category)).to<Table>();

	return (int) mod.get(str_key(name)).to<int64_t>();
}

bool Settings::get_boolean(const String &category, const String &name)
{
	auto mod = fetch_settings(runtime).get(str_key(category)).to<Table>();

	return mod.get(str_key(name)).to<bool>();
}

double Settings::get_number(const String &category, const String &name)
{
	auto mod = fetch_settings(runtime).get(str_key(category)).to<Table>();

	return mod.get(str_key(name)).to<double>();
}

String Settings::get_string(const String &category, const String &name)
{
	auto mod = fetch_settings(runtime).get(str_key(category)).to<Table>();

	return mod.get(str_key(name)).to<String>();
}

void Settings::set_value(const String &category, const String &key, Variant value)
{
	auto settings = fetch_settings(runtime);
	auto cat_v = settings.get(str_key(category));
	// Self-heal: if the category table does not exist yet, create it. This can happen
	// when a settings file written by an earlier version is loaded and a new category
	// (e.g. "font") has been added since.
	Table mod = cat_v.is_null() ? Table() : cat_v.to<Table>();
	mod.set(str_key(key), std::move(value));
	settings.set(str_key(category), Variant::make(mod));
	store_settings(runtime, settings);
}

String Settings::get_std_plugin_directory()
{
#if PHON_LINUX
	String name("plugins");
#else
	String name("Plugins");
#endif

	return filesystem::join(Settings::resources_directory(), name);
}

// The old engine's dump_json wrote floats with a bare trailing decimal point
// ("2259.", "0."). The new engine's lexer treats '.' as a decimal point only
// when a digit follows (so `1.method()` is never mis-lexed), which makes such
// files syntax errors — and Settings::read would silently reset the user's
// settings to defaults. Settings files are machine-written, so repair them by
// inserting the missing '0'. String-aware: quoted values (e.g. paths like
// "take1.wav") are copied verbatim.
static String normalize_legacy_floats(const String &content)
{
	std::string out;
	out.reserve(content.size() + 16);
	const char *s = content.data();
	intptr_t n = content.size();
	bool in_string = false;

	for (intptr_t i = 0; i < n; i++)
	{
		char c = s[i];
		out.push_back(c);

		if (in_string)
		{
			if (c == '\\' && i + 1 < n)
				out.push_back(s[++i]); // copy escape pair verbatim
			else if (c == '"')
				in_string = false;
		}
		else if (c == '"')
		{
			in_string = true;
		}
		else if (c == '.' && i > 0 && isdigit((unsigned char) s[i-1])
				 && (i + 1 == n || !isdigit((unsigned char) s[i+1])))
		{
			out.push_back('0');
		}
	}

	return String(out);
}

void Settings::read()
{
	// `read_settings_script` must always be embedded because we need the resources directory
	// to be set before we can load a script from disk.
	String content;
	auto path = config_path();

	if (filesystem::exists(path))
	{
		content = normalize_legacy_floats(File::read_all(path));
		if (content.trim().empty())
		{
            content = load_script("read_settings");
		}
	}
	else
	{
        content = load_script("read_settings");
	}
	Variant result;

	try
	{
		result = runtime->do_string(content);
	}
	catch (std::exception &)
	{
		// TODO: notify user that settings are invalid and have been reinitialized.
		result = runtime->do_string(load_script("read_settings"));
	}
	// Versions of Phonometrica prior to 0.8 created phon.settings in settings.phon.
	// We now simply store a table in this file, and create settings.phon ourselves to
	// hide it from users.
	if (result.is_null())
	{
		// Sanity check: the script must have installed phon.settings itself.
		auto phon = runtime->get_global("phon");
		if (phon.is_null() || phon.to<Table>().get(str_key(settings_key)).is_null()) {
			throw error("Settings could not be initialized properly: check the file '%'", config_path());
		}
	}
	else
	{
		auto phon = runtime->get_global("phon").to<Table>();
		phon.set(str_key(settings_key), std::move(result));
		runtime->add_global("phon", Variant::make(phon));
	}
}

void Settings::write()
{
	run_script((*runtime), write_settings);
}

String Settings::get_documentation_page(String page)
{
	page = filesystem::nativize(page);
	auto path = filesystem::join(resources_directory(), "html", page);
	if (!path.ends_with(".html")) {
		filesystem::append(path, "index.html");
	}
	path.prepend("file://");

	return path;
}

String Settings::resources_directory()
{
	return std_resource_path;
}

void Settings::post_initialize()
{
	// Ensure that every settings category used by the application is
	// present. This is the migration path for users upgrading from an
	// earlier version whose settings file predates one or more categories.
	//
	// Philosophy: we check whether the top-level key exists and, if not,
	// run the corresponding reset_* function to populate platform defaults.
	// We never overwrite an existing table, so user customisations are
	// preserved across upgrades. A few categories have grown new sub-keys
	// over time; for those, we additionally probe a representative newer
	// key and reset the whole table if that key is absent.
	//
	// The self-healing Settings::set_value(category, key, value) is the
	// second line of defence: writes to a category whose table is present
	// but missing the sub-key no longer crash — they insert the sub-key
	// lazily. This migration just guarantees sensible defaults on reads.
	//
	// The settings table is CoW: re-fetch it on every probe (a reset_*
	// call replaces the stored table, so a cached copy would be stale).

	auto contains = [](const char *k) {
		return fetch_settings(Settings::runtime).contains(str_key(k));
	};

	// --- Window layout and session state ---------------------------------
	// reset_geometry writes eight scalars directly at the top level of
	// `settings`. "project_ratio" is used as the canary for the whole set.
	if (!contains("project_ratio")) {
		reset_geometry();
	}
	if (!contains("restore_views")) {
		reset_recent_views();
	}
	if (!contains("recent_projects")) {
		reset_recent_projects();
	}

	// --- Top-level scalar preferences ------------------------------------
	if (!contains("autohints")) {
		reset_autohints();
	}
	if (!contains("autoload")) {
		reset_autoload();
	}
	if (!contains("autosave")) {
		reset_autosave();
	}
	if (!contains("last_directory")) {
		reset_last_directory();
	}
	if (!contains("enable_mouse_tracking")) {
		reset_mouse_tracking();
	}
	if (!contains("check_for_updates")) {
		reset_check_for_updates();
	}

	// --- Appearance ------------------------------------------------------
	if (!contains("font")) {
		// Added after initial release: users upgrading from a version that
		// predates the font preference will not have this table. Without
		// this migration, reading "font"/"name" or "font"/"size" throws,
		// and saving preferences would crash before set_value became
		// self-healing.
		reset_mono_font();
	}

	// --- Signal analysis tables ------------------------------------------
	if (!contains("waveform")) {
		reset_waveform();
	}
	if (!contains("pitch_tracking")) {
		reset_pitch_tracking();
	}
	if (!contains("spectrogram")) {
		reset_spectrogram();
	}
	if (!contains("intensity")) {
		reset_intensity();
	}

	// sound_plots gained "formants", "pitch" and "intensity" sub-keys in
	// 0.8; probe a late-added key so that pre-0.8 tables are refreshed
	// rather than silently carrying partial state.
	if (!contains("sound_plots")) {
		reset_sound_plots();
	}
	else {
		try {
			Settings::get_boolean("sound_plots", "intensity");
		}
		catch (...) {
			reset_sound_plots();
		}
	}

	// formants gained "time_step" in 0.8.
	if (!contains("formants")) {
		reset_formants();
	}
	else {
		try {
			Settings::get_number("formants", "time_step");
		}
		catch (...) {
			reset_formants();
		}
	}

	// --- Concordance, display, statistics --------------------------------
	if (!contains("concordance")) {
		reset_concordance();
	}
	if (!contains("display")) {
		reset_display();
	}
	if (!contains("statistics")) {
		reset_statistics();
	}
	if (!contains("whisper_log")) {
		reset_whisper_log();
	}

	if (!contains("recording")) {
		reset_recording();
	}

	// concordance gained "default_context" in 0.9.
	try {
		Settings::get_string("concordance", "default_context");
	}
	catch (...) {
		Settings::set_value("concordance", "default_context", Variant::make(String("kwic")));
	}
}

void Settings::reset()
{
	reset_geometry();
	reset_recent_projects();
	reset_mono_font();
	reset_autohints();
	reset_autoload();
	reset_autosave();
	reset_last_directory();
	reset_waveform();
	reset_sound_plots();
	reset_pitch_tracking();
	reset_formants();
	reset_spectrogram();
	reset_intensity();
	reset_mouse_tracking();
	reset_concordance();
	reset_display();
	reset_statistics();
	reset_whisper_log();
	reset_check_for_updates();
	reset_recording();
}

void Settings::reset_waveform()
{
	Table table;
	table.set(str_key("magnitude"), Variant::make(1.0));
	table.set(str_key("scaling"), Variant::make(String("local")));

	Settings::set_value("waveform", Variant::make(table));
}

void Settings::reset_mono_font()
{
	Table table;
#if PHON_MACOS
	table.set(str_key("name"), Variant::make(String("Monaco")));
	table.set(str_key("size"), Variant::make<int64_t>(13));
#elif PHON_WINDOWS
	table.set(str_key("name"), Variant::make(String("Consolas")));
	table.set(str_key("size"), Variant::make<int64_t>(10));
#else
	table.set(str_key("name"), Variant::make(String("Monospace")));
	table.set(str_key("size"), Variant::make<int64_t>(12));
#endif
	Settings::set_value("font", Variant::make(table));
}

void Settings::reset_autohints()
{
	Settings::set_value("autohints", Variant::make(true));
}

void Settings::reset_whisper_log()
{
	// Whisper + ggml normally print diagnostic output to stderr (model load sizes, compute
	// buffer allocations, etc.). Silenced by default; when toggled on, routed to the
	// Phonometrica output panel — never back to stderr/stdout.
	Settings::set_value("whisper_log", Variant::make(false));
}

void Settings::reset_check_for_updates()
{
	Settings::set_value("check_for_updates", Variant::make(true));
}

void Settings::reset_autoload()
{
	Settings::set_value("autoload", Variant::make(false));
}

void Settings::reset_autosave()
{
	Settings::set_value("autosave", Variant::make(false));
}

void Settings::reset_recent_views()
{
	auto settings = fetch_settings(runtime);
	settings.set(str_key("restore_views"), Variant::make(false));
	settings.set(str_key("recent_views"), Variant::make(List()));
	settings.set(str_key("selected_view"), Variant::make<int64_t>(-1));
	store_settings(runtime, settings);
}

void Settings::reset_geometry()
{
	auto settings = fetch_settings(runtime);
	settings.set(str_key("project_ratio"), Variant::make(0.17));
	settings.set(str_key("console_ratio"), Variant::make(0.80));
	settings.set(str_key("info_ratio"), Variant::make(0.80));
	settings.set(str_key("full_screen"), Variant::make(true));
	settings.set(str_key("hide_project"), Variant::make(false));
	settings.set(str_key("hide_console"), Variant::make(false));
	settings.set(str_key("hide_info"), Variant::make(false));
	List geo = { Variant::make(0.0), Variant::make(0.0), Variant::make(0.0), Variant::make(0.0) };
	settings.set(str_key("geometry"), Variant::make(geo));
	store_settings(runtime, settings);
}

void Settings::reset_concordance()
{
	Table table;
	table.set(str_key("context_length"), Variant::make<int64_t>(40));
	table.set(str_key("default_context"), Variant::make(String("kwic")));
	table.set(str_key("discard_empty"), Variant::make(true));
	Settings::set_value("concordance", Variant::make(table));
}

void Settings::reset_display()
{
	Table table;
	table.set(str_key("hz_decimals"), Variant::make<int64_t>(0)); // 0 = round to nearest Hz
	Settings::set_value("display", Variant::make(table));
}

void Settings::reset_statistics()
{
	Table table;
	table.set(str_key("estimation"), Variant::make(String("frequentist")));
	table.set(str_key("max_iterations"), Variant::make<int64_t>(200));
	Settings::set_value("statistics", Variant::make(table));
}

void Settings::reset_mouse_tracking()
{
	Settings::set_value("enable_mouse_tracking", Variant::make(true));
}

void Settings::reset_sound_plots()
{
	Table table;
	table.set(str_key("waveform"), Variant::make(true));
	table.set(str_key("spectrogram"), Variant::make(true));
	table.set(str_key("formants"), Variant::make(true));
	table.set(str_key("pitch"), Variant::make(true));
	table.set(str_key("intensity"), Variant::make(true));

	Settings::set_value("sound_plots", Variant::make(table));
}

void Settings::reset_last_directory()
{
	Settings::set_value("last_directory", Variant::make(String()));
}

void Settings::reset_pitch_tracking()
{
	Table table;
	table.set(str_key("method"), Variant::make(String("praat")));
	table.set(str_key("minimum_pitch"), Variant::make<int64_t>(70));
	table.set(str_key("maximum_pitch"), Variant::make<int64_t>(500));
	table.set(str_key("time_step"), Variant::make(0.01));
	table.set(str_key("voicing_threshold"), Variant::make(0.45));
	table.set(str_key("octave_jump_cost"), Variant::make(0.35));
	table.set(str_key("voicing_cost"), Variant::make(0.14));
	table.set(str_key("silence_threshold"), Variant::make(0.03));
	table.set(str_key("octave_cost"), Variant::make(0.01));
	table.set(str_key("use_gaussian"), Variant::make(false));

	Settings::set_value("pitch_tracking", Variant::make(table));
}

void Settings::reset_formants()
{
	Table table;
	table.set(str_key("number_of_formants"), Variant::make<int64_t>(4));
	table.set(str_key("window_size"), Variant::make(0.025));
	table.set(str_key("lpc_order"), Variant::make<int64_t>(10));
	table.set(str_key("max_frequency"), Variant::make<int64_t>(5500));
	table.set(str_key("time_step"), Variant::make(0.01));

	Settings::set_value("formants", Variant::make(table));
}

void Settings::reset_spectrogram()
{
	Table table;
	table.set(str_key("window_size"), Variant::make(0.005));
	table.set(str_key("frequency_range"), Variant::make<int64_t>(5500));
	table.set(str_key("window_type"), Variant::make(String("Gaussian")));
	table.set(str_key("dynamic_range"), Variant::make<int64_t>(70));
	table.set(str_key("preemphasis_threshold"), Variant::make<int64_t>(1000));

	Settings::set_value("spectrogram", Variant::make(table));
}

void Settings::reset_intensity()
{
	Table table;
	table.set(str_key("minimum_intensity"), Variant::make<int64_t>(50));
	table.set(str_key("maximum_intensity"), Variant::make<int64_t>(100));
	table.set(str_key("time_step"), Variant::make(0.01));

	Settings::set_value("intensity", Variant::make(table));
}

void Settings::reset_recording()
{
	// Tunables for the SoundRecorder. block_frames is the chunk size handed to
	// libsndfile per write; 4096 frames at 44.1 kHz mono float = 16 KB, well
	// matched to filesystem block sizes. pool_blocks * block_frames bounds the
	// in-flight buffer between the audio thread and the writer thread; the
	// default ~5 s of headroom at 48 kHz stereo absorbs typical disk stalls
	// (filesystem journal flush, antivirus scan) without dropping frames.
	Table table;
	table.set(str_key("block_frames"), Variant::make<int64_t>(4096));
	table.set(str_key("pool_blocks"), Variant::make<int64_t>(64));
	table.set(str_key("default_format"), Variant::make(String("WAV")));
	table.set(str_key("default_sample_rate"), Variant::make<int64_t>(44100));

	Settings::set_value("recording", Variant::make(table));
}

void Settings::reset_recent_projects()
{
	Settings::set_value("recent_projects", Variant::make(List()));
}

} // namespace phonometrica
